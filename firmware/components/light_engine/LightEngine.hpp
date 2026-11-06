#pragma once
#include <chrono>
#include <cstdint>
#include "LightModel.hpp"
#include "Mutex.hpp"
#include "StaticTask.hpp"
#include "esp_err.h"
#include "led_strip.h"

/**
 * Renders a LightState onto a WS2812 strip in its own task.
 *
 * - Transitions fade linearly over time in (pre-gamma) output space.
 * - Gamma 2.2 correction so brightness steps look even.
 * - Total-current estimate is capped at max_milliamps by scaling the frame.
 */
class LightEngine {
public:
    struct Config {
        int gpio;
        uint16_t led_count;
        uint32_t max_milliamps;       ///< 0 = no limit
        uint8_t ma_per_channel = 20;  ///< WS2812: ~20 mA per channel at full duty
    };

    LightEngine() = default;
    LightEngine(const LightEngine&) = delete;
    LightEngine& operator=(const LightEngine&) = delete;

    [[nodiscard]] esp_err_t start(const Config& cfg);

    /** Fade from whatever is currently shown to `s` over `transition`. Thread-safe. */
    void set(const LightState& s, std::chrono::milliseconds transition);

    [[nodiscard]] std::size_t stack_free_bytes() const { return task_.stack_free_bytes(); }

private:
    static constexpr const char* TAG = "light_engine";
    static constexpr int FRAME_MS = 16;
    static constexpr std::chrono::seconds REFRESH_PERIOD{1};
    static constexpr std::size_t TASK_STACK_BYTES = 4096;
    static constexpr UBaseType_t TASK_PRIORITY = 10;

    PowerBudget power_{};
    uint32_t led_count_ = 0;
    led_strip_handle_t strip_ = nullptr;
    bool refresh_failed_ = false;  // only touched by the render task

    rtos::Mutex mtx_;  // guards the fade below
    Levels from_{}, to_{}, cur_{};
    std::chrono::microseconds fade_start_{0}, fade_duration_{0};

    rtos::StaticTask<TASK_STACK_BYTES> task_;

    [[noreturn]] void render_loop();
    void write_frame(const Rgb& pixel);
};
