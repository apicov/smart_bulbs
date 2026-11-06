#include "BulbCtrl.hpp"
#include <cstdint>
#include <mutex>
#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"

namespace {
constexpr const char* NVS_NS = "bulb";
constexpr const char* NVS_KEY = "state";

/** Scoped handle on the bulb's NVS namespace. */
class NvsHandle {
public:
    explicit NvsHandle(nvs_open_mode_t mode) : opened_{nvs_open(NVS_NS, mode, &handle_) == ESP_OK} {}
    ~NvsHandle() {
        if (opened_) nvs_close(handle_);
    }
    NvsHandle(const NvsHandle&) = delete;
    NvsHandle& operator=(const NvsHandle&) = delete;

    explicit operator bool() const { return opened_; }
    [[nodiscard]] nvs_handle_t get() const { return handle_; }

private:
    nvs_handle_t handle_ = 0;
    bool opened_;
};
}  // namespace

esp_err_t BulbCtrl::start(bool power_on_light, StateListener on_change) {
    on_change_ = on_change;

    esp_timer_create_args_t args = {};
    args.callback = [](void* self) { static_cast<BulbCtrl*>(self)->save(); };
    args.arg = this;
    args.name = "bulb_save";
    ESP_RETURN_ON_ERROR(esp_timer_create(&args, &save_timer_), TAG, "save timer");

    load();
    // A bulb behind a wall switch should light up when power returns.
    if (power_on_light) state_.on = true;
    engine_.set(state_, BOOT_FADE);
    return ESP_OK;
}

bool BulbCtrl::apply_json(std::string_view json) {
    bool changed = false;
    StateJson report;
    {
        const std::lock_guard lock{mtx_};
        const auto cmd = parse_command(json, state_);
        if (!cmd) {
            ESP_LOGW(TAG, "Ignoring invalid command: %.*s", static_cast<int>(json.size()), json.data());
            return false;
        }
        changed = cmd->state != state_;
        state_ = cmd->state;
        engine_.set(state_, cmd->transition);
        report = format_state(state_);
    }

    if (changed) {
        esp_timer_stop(save_timer_);  // restart the debounce window
        esp_timer_start_once(save_timer_, static_cast<uint64_t>(SAVE_DELAY.count()));
    }
    // Always report back, even if unchanged, so the app can reconcile optimistic UI.
    if (on_change_) on_change_(report.view());
    return true;
}

StateJson BulbCtrl::state_json() {
    const std::lock_guard lock{mtx_};
    return format_state(state_);
}

void BulbCtrl::load() {
    const NvsHandle nvs{NVS_READONLY};
    if (!nvs) {
        ESP_LOGI(TAG, "No saved state, using defaults");
        return;
    }
    StoredState rec;
    size_t size = sizeof rec;
    if (nvs_get_blob(nvs.get(), NVS_KEY, &rec, &size) == ESP_OK && size == sizeof rec &&
        rec.version == StoredState::VERSION) {
        state_ = rec.state;
        const StateJson json = format_state(state_);
        ESP_LOGI(TAG, "Restored state: %.*s", static_cast<int>(json.view().size()), json.view().data());
    }
}

void BulbCtrl::save() {
    StoredState rec;
    {
        const std::lock_guard lock{mtx_};
        rec.state = state_;
    }

    const NvsHandle nvs{NVS_READWRITE};
    if (!nvs || nvs_set_blob(nvs.get(), NVS_KEY, &rec, sizeof rec) != ESP_OK ||
        nvs_commit(nvs.get()) != ESP_OK) {
        ESP_LOGW(TAG, "Could not save state");
        return;
    }
    ESP_LOGD(TAG, "State saved");
}
