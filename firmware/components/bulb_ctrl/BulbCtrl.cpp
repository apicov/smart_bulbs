#include "BulbCtrl.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include "cJSON.h"
#include "esp_log.h"
#include "nvs.h"

namespace {
// Versioned NVS record so the struct can grow later without misreading old data.
struct Stored {
    uint8_t version = 1;
    LightState s;
};

uint8_t to_u8(const cJSON* n) {
    return static_cast<uint8_t>(std::clamp<int>(static_cast<int>(n->valuedouble), 0, 255));
}

bool parse_color(const cJSON* c, Rgb& out) {
    if (cJSON_IsArray(c) && cJSON_GetArraySize(c) == 3) {
        const cJSON* v[3] = {cJSON_GetArrayItem(c, 0), cJSON_GetArrayItem(c, 1),
                             cJSON_GetArrayItem(c, 2)};
        if (!cJSON_IsNumber(v[0]) || !cJSON_IsNumber(v[1]) || !cJSON_IsNumber(v[2])) return false;
        out = {to_u8(v[0]), to_u8(v[1]), to_u8(v[2])};
        return true;
    }
    if (cJSON_IsObject(c)) {
        const cJSON* r = cJSON_GetObjectItem(c, "r");
        const cJSON* g = cJSON_GetObjectItem(c, "g");
        const cJSON* b = cJSON_GetObjectItem(c, "b");
        if (!cJSON_IsNumber(r) || !cJSON_IsNumber(g) || !cJSON_IsNumber(b)) return false;
        out = {to_u8(r), to_u8(g), to_u8(b)};
        return true;
    }
    return false;
}
}  // namespace

void BulbCtrl::begin(LightEngine& engine, bool power_on_light, StateListener on_change) {
    engine_ = &engine;
    on_change_ = std::move(on_change);
    mtx_ = xSemaphoreCreateMutex();

    esp_timer_create_args_t args = {};
    args.callback = save_cb;
    args.arg = this;
    args.name = "bulb_save";
    ESP_ERROR_CHECK(esp_timer_create(&args, &save_timer_));

    load();
    // A bulb behind a wall switch should light up when power returns.
    if (power_on_light) state_.on = true;
    engine_->set(state_, BOOT_FADE_MS);
}

bool BulbCtrl::apply_json(const char* data, int len) {
    cJSON* root = cJSON_ParseWithLength(data, len);
    if (!root || !cJSON_IsObject(root)) {
        ESP_LOGW(TAG, "Ignoring invalid command: %.*s", len, data);
        cJSON_Delete(root);
        return false;
    }

    xSemaphoreTake(mtx_, portMAX_DELAY);
    LightState next = state_;

    const cJSON* st = cJSON_GetObjectItem(root, "state");
    if (cJSON_IsString(st)) {
        if (!strcasecmp(st->valuestring, "ON")) next.on = true;
        else if (!strcasecmp(st->valuestring, "OFF")) next.on = false;
        else if (!strcasecmp(st->valuestring, "TOGGLE")) next.on = !next.on;
    } else if (cJSON_IsBool(st)) {
        next.on = cJSON_IsTrue(st);
    }

    const cJSON* br = cJSON_GetObjectItem(root, "brightness");
    if (cJSON_IsNumber(br)) {
        const uint8_t b = to_u8(br);
        if (b == 0) next.on = false;  // keep the last non-zero brightness for the next "ON"
        else next.brightness = b;
    }

    Rgb c;
    if (parse_color(cJSON_GetObjectItem(root, "color"), c)) next.color = c;

    uint32_t transition_ms = DEFAULT_TRANSITION_MS;
    const cJSON* tr = cJSON_GetObjectItem(root, "transition");
    if (cJSON_IsNumber(tr))
        transition_ms = static_cast<uint32_t>(std::clamp(tr->valuedouble, 0.0, 3600.0) * 1000.0);

    cJSON_Delete(root);

    const bool changed = !(next == state_);
    state_ = next;
    engine_->set(state_, transition_ms);
    const std::string json = state_json_locked();
    xSemaphoreGive(mtx_);

    if (changed) {
        esp_timer_stop(save_timer_);  // restart the debounce window
        esp_timer_start_once(save_timer_, SAVE_DELAY_US);
    }
    // Always report back, even if unchanged, so the app can reconcile optimistic UI.
    if (on_change_) on_change_(json);
    return true;
}

std::string BulbCtrl::state_json() {
    xSemaphoreTake(mtx_, portMAX_DELAY);
    std::string s = state_json_locked();
    xSemaphoreGive(mtx_);
    return s;
}

std::string BulbCtrl::state_json_locked() const {
    char buf[128];
    snprintf(buf, sizeof buf,
             R"({"state":"%s","brightness":%u,"color_mode":"rgb","color":{"r":%u,"g":%u,"b":%u}})",
             state_.on ? "ON" : "OFF", state_.brightness, state_.color.r, state_.color.g,
             state_.color.b);
    return buf;
}

void BulbCtrl::load() {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "No saved state, using defaults");
        return;
    }
    Stored rec;
    size_t sz = sizeof rec;
    if (nvs_get_blob(h, NVS_KEY, &rec, &sz) == ESP_OK && sz == sizeof rec && rec.version == 1) {
        state_ = rec.s;
        ESP_LOGI(TAG, "Restored state: %s", state_json_locked().c_str());
    }
    nvs_close(h);
}

void BulbCtrl::save_cb(void* self) { static_cast<BulbCtrl*>(self)->save(); }

void BulbCtrl::save() {
    Stored rec;
    xSemaphoreTake(mtx_, portMAX_DELAY);
    rec.s = state_;
    xSemaphoreGive(mtx_);

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_blob(h, NVS_KEY, &rec, sizeof rec) == ESP_OK) nvs_commit(h);
    nvs_close(h);
    ESP_LOGD(TAG, "State saved");
}
