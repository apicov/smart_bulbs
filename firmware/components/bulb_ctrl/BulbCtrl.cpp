#include "BulbCtrl.hpp"
#include <algorithm>
#include <cstdint>
#include <ctime>
#include <mutex>
#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"

namespace {
constexpr const char* NVS_NS = "bulb";
constexpr const char* NVS_KEY = "state";
constexpr const char* NVS_ALARM_KEY = "alarm";

// 2024-01-01 UTC. The clock starts near 1970, so anything later has been set by NTP.
constexpr std::time_t CLOCK_VALID_AFTER = 1704067200;
constexpr TickType_t SCHEDULE_PERIOD = pdMS_TO_TICKS(1000);

std::chrono::microseconds monotonic_now() { return std::chrono::microseconds{esp_timer_get_time()}; }

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

esp_err_t BulbCtrl::start(bool power_on_light, JsonListener on_state, JsonListener on_schedule) {
    on_state_ = on_state;
    on_schedule_ = on_schedule;

    esp_timer_create_args_t args = {};
    args.callback = [](void* self) { static_cast<BulbCtrl*>(self)->save(); };
    args.arg = this;
    args.name = "bulb_save";
    ESP_RETURN_ON_ERROR(esp_timer_create(&args, &save_timer_), TAG, "save timer");

    load();
    // A bulb behind a wall switch should light up when power returns.
    if (power_on_light) state_.on = true;
    engine_.set(state_, BOOT_FADE);

    task_.start<&BulbCtrl::schedule_loop>("schedule", TASK_PRIORITY, this);
    return ESP_OK;
}

bool BulbCtrl::apply_json(std::string_view json) {
    Applied applied;
    {
        const std::lock_guard lock{mtx_};
        const auto cmd = parse_command(json, state_);
        if (!cmd) {
            ESP_LOGW(TAG, "Ignoring invalid command: %.*s", static_cast<int>(json.size()), json.data());
            return false;
        }
        applied = apply_locked(cmd->state, cmd->transition);
    }
    finish_apply(applied);
    return true;
}

bool BulbCtrl::apply_schedule_json(std::string_view json) {
    bool alarm_changed = false;
    Alarm alarm;
    {
        const std::lock_guard lock{mtx_};
        const auto cmd = parse_schedule_command(json, alarm_);
        if (!cmd) {
            ESP_LOGW(TAG, "Ignoring invalid schedule: %.*s", static_cast<int>(json.size()), json.data());
            return false;
        }

        using enum ScheduleCommand::TimerAction;
        if (cmd->timer_action == Cancel) {
            timer_active_ = false;
        } else if (cmd->timer_action == Start && state_.on) {  // a light that's off has nothing to turn off
            timer_active_ = true;
            timer_fade_s_ = cmd->timer.fade_s;
            timer_fade_start_ =
                monotonic_now() + std::chrono::seconds{cmd->timer.after_s - cmd->timer.fade_s};
        }

        if (cmd->alarm) {
            alarm_changed = *cmd->alarm != alarm_;
            alarm_ = *cmd->alarm;
            alarm = alarm_;
            rise_active_ = false;  // a new alarm may already be inside its rise
        }
    }
    if (alarm_changed) save_alarm(alarm);
    publish_schedule();  // always, so the app can reconcile
    return true;
}

BulbCtrl::Applied BulbCtrl::apply_locked(const LightState& next, std::chrono::milliseconds transition) {
    Applied applied;
    applied.changed = next != state_;
    scheduled_fade_ends_ = std::chrono::microseconds{0};
    state_ = next;
    engine_.set(state_, transition);
    applied.report = format_state(state_);
    if (!next.on && timer_active_) {  // nothing left for the timer to turn off
        timer_active_ = false;
        applied.schedule_changed = true;
    }
    return applied;
}

void BulbCtrl::finish_apply(const Applied& applied) {
    if (applied.changed) {
        esp_timer_stop(save_timer_);  // restart the debounce window
        esp_timer_start_once(save_timer_, static_cast<uint64_t>(SAVE_DELAY.count()));
    }
    // Always report back, even if unchanged, so the app can reconcile optimistic UI.
    if (on_state_) on_state_(applied.report.view());
    if (applied.schedule_changed) publish_schedule();
}

void BulbCtrl::flush() {
    if (esp_timer_is_active(save_timer_)) {
        esp_timer_stop(save_timer_);
        save();
    }
}

bool BulbCtrl::schedule_active() {
    const std::lock_guard lock{mtx_};
    return timer_active_ || monotonic_now() < scheduled_fade_ends_;
}

StateJson BulbCtrl::state_json() {
    const std::lock_guard lock{mtx_};
    return format_state(state_);
}

ScheduleJson BulbCtrl::schedule_json() {
    const std::lock_guard lock{mtx_};
    return schedule_json_locked();
}

ScheduleJson BulbCtrl::schedule_json_locked() {
    const std::time_t now = std::time(nullptr);
    const bool synced = now >= CLOCK_VALID_AFTER;

    std::optional<TimerStatus> timer;
    if (timer_active_) {
        const auto to_fade =
            std::chrono::duration_cast<std::chrono::seconds>(timer_fade_start_ - monotonic_now());
        const int64_t remaining = std::max<int64_t>(to_fade.count(), 0) + timer_fade_s_;
        timer =
            TimerStatus{.ends = synced ? static_cast<int64_t>(now) + remaining : 0, .fade_s = timer_fade_s_};
    }
    return format_schedule(synced, timer, alarm_);
}

void BulbCtrl::publish_schedule() {
    const ScheduleJson json = schedule_json();
    if (on_schedule_) on_schedule_(json.view());
}

void BulbCtrl::schedule_loop() {
    TickType_t last_wake = xTaskGetTickCount();
    for (;;) {
        check_clock();
        check_sleep_timer();
        check_alarm();
        vTaskDelayUntil(&last_wake, SCHEDULE_PERIOD);
    }
}

void BulbCtrl::check_clock() {
    if (clock_valid_) return;
    const std::time_t now = std::time(nullptr);
    if (now < CLOCK_VALID_AFTER) return;
    clock_valid_ = true;

    std::tm local{};
    if (localtime_r(&now, &local) == nullptr) return;
    std::array<char, 40> text{};
    (void)std::strftime(text.data(), text.size(), "%a %Y-%m-%d %H:%M:%S %Z", &local);
    ESP_LOGI(TAG, "Clock set: %s", text.data());
    publish_schedule();  // the app can stop warning, and timer end times are now known
}

void BulbCtrl::check_sleep_timer() {
    Applied applied;
    {
        const std::lock_guard lock{mtx_};
        if (!timer_active_ || monotonic_now() < timer_fade_start_) return;
        timer_active_ = false;
        LightState next = state_;
        next.on = false;
        applied = apply_locked(next, std::chrono::seconds{timer_fade_s_});
        scheduled_fade_ends_ = monotonic_now() + std::chrono::seconds{timer_fade_s_};
        applied.schedule_changed = true;
    }
    ESP_LOGI(TAG, "Sleep timer: turning off");
    finish_apply(applied);
}

void BulbCtrl::check_alarm() {
    const std::time_t now = std::time(nullptr);
    if (now < CLOCK_VALID_AFTER) return;
    std::tm local{};
    if (localtime_r(&now, &local) == nullptr) return;
    const uint32_t week_second = second_of_week(local.tm_wday, local.tm_hour, local.tm_min, local.tm_sec);

    Applied applied;
    Alarm alarm;
    bool once_used = false;
    uint32_t rise_s = 0;
    {
        const std::lock_guard lock{mtx_};
        const auto remaining = alarm_rise_remaining(alarm_, week_second);
        const bool starting = remaining.has_value() && !rise_active_;
        rise_active_ = remaining.has_value();
        if (!starting) return;

        rise_s = *remaining;
        LightState next = state_;
        next.on = true;
        next.color = alarm_.color;
        next.brightness = alarm_.brightness;
        applied = apply_locked(next, std::chrono::seconds{rise_s});
        scheduled_fade_ends_ = monotonic_now() + std::chrono::seconds{rise_s};
        timer_active_ = false;  // a timer shouldn't switch off the wake-up light
        applied.schedule_changed = true;
        if (alarm_.days == 0) {
            alarm_.enabled = false;
            once_used = true;
        }
        alarm = alarm_;
    }
    ESP_LOGI(TAG, "Alarm: rising over %lu s", static_cast<unsigned long>(rise_s));
    if (once_used) save_alarm(alarm);
    finish_apply(applied);
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

    StoredAlarm stored_alarm;
    size = sizeof stored_alarm;
    if (nvs_get_blob(nvs.get(), NVS_ALARM_KEY, &stored_alarm, &size) == ESP_OK &&
        size == sizeof stored_alarm && stored_alarm.version == StoredAlarm::VERSION) {
        alarm_ = stored_alarm.alarm;
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

void BulbCtrl::save_alarm(const Alarm& alarm) {
    const StoredAlarm rec{.alarm = alarm};
    const NvsHandle nvs{NVS_READWRITE};
    if (!nvs || nvs_set_blob(nvs.get(), NVS_ALARM_KEY, &rec, sizeof rec) != ESP_OK ||
        nvs_commit(nvs.get()) != ESP_OK) {
        ESP_LOGW(TAG, "Could not save alarm");
    }
}
