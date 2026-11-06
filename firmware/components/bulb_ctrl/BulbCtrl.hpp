#pragma once
#include <chrono>
#include <string_view>
#include "BulbProtocol.hpp"
#include "Callback.hpp"
#include "LightEngine.hpp"
#include "Mutex.hpp"
#include "esp_err.h"
#include "esp_timer.h"

/**
 * Owns the bulb's LightState: applies JSON commands, persists to NVS and
 * reports state as JSON. See BulbProtocol.hpp for the formats.
 */
class BulbCtrl {
public:
    using StateListener = Callback<void(std::string_view state_json)>;

    explicit BulbCtrl(LightEngine& engine) : engine_{engine} {}
    BulbCtrl(const BulbCtrl&) = delete;
    BulbCtrl& operator=(const BulbCtrl&) = delete;

    /** Load state from NVS (or defaults) and show it. */
    [[nodiscard]] esp_err_t start(bool power_on_light, StateListener on_change);

    /** Apply a `set` command. Returns false if the payload is not valid JSON. */
    bool apply_json(std::string_view json);

    [[nodiscard]] StateJson state_json();

private:
    static constexpr const char* TAG = "bulb_ctrl";
    static constexpr std::chrono::milliseconds BOOT_FADE{1000};
    static constexpr std::chrono::microseconds SAVE_DELAY = std::chrono::seconds{2};

    LightEngine& engine_;
    StateListener on_change_;
    rtos::Mutex mtx_;  // guards state_
    LightState state_{};
    esp_timer_handle_t save_timer_ = nullptr;

    void load();
    void save();
};
