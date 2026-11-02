#pragma once
#include <functional>
#include <string>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "LightEngine.hpp"

/**
 * Owns the bulb's LightState: applies JSON commands, persists to NVS and
 * reports state as JSON.
 *
 * Wire format follows Home Assistant's MQTT "json" light schema:
 *   set:   {"state":"ON"|"OFF"|"TOGGLE", "brightness":0-255,
 *           "color":{"r":..,"g":..,"b":..} (or [r,g,b]), "transition":seconds}
 *   state: {"state":"ON","brightness":128,"color_mode":"rgb","color":{"r":..,"g":..,"b":..}}
 * All set fields are optional.
 */
class BulbCtrl {
public:
    using StateListener = std::function<void(const std::string& state_json)>;

    /** Load state from NVS (or defaults) and show it. */
    void begin(LightEngine& engine, bool power_on_light, StateListener on_change);

    /** Apply a `set` command. Returns false if the payload is not valid JSON. */
    bool apply_json(const char* data, int len);

    std::string state_json();

private:
    static constexpr const char* TAG = "bulb_ctrl";
    static constexpr const char* NVS_NS = "bulb";
    static constexpr const char* NVS_KEY = "state";
    static constexpr uint32_t DEFAULT_TRANSITION_MS = 400;
    static constexpr uint32_t BOOT_FADE_MS = 1000;
    static constexpr uint64_t SAVE_DELAY_US = 2 * 1000 * 1000;

    LightEngine* engine_ = nullptr;
    StateListener on_change_;
    SemaphoreHandle_t mtx_ = nullptr;
    LightState state_{};
    esp_timer_handle_t save_timer_ = nullptr;

    void load();
    void save();
    static void save_cb(void* self);
    std::string state_json_locked() const;
};
