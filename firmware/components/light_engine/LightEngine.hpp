#pragma once
#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "led_strip.h"

struct Rgb {
    uint8_t r, g, b;
    bool operator==(const Rgb&) const = default;
};

/** User-facing light state (what the app sets, what NVS stores). */
struct LightState {
    bool on = true;
    Rgb color{255, 180, 100};  // warm white
    uint8_t brightness = 128;  // 0..255
    bool operator==(const LightState&) const = default;
};

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
        uint32_t max_milliamps;     ///< 0 = no limit
        uint8_t ma_per_channel = 20;  ///< WS2812: ~20 mA per channel at full duty
    };

    esp_err_t start(const Config& cfg);

    /** Fade from whatever is currently shown to `s` over `transition_ms`. Thread-safe. */
    void set(const LightState& s, uint32_t transition_ms);

private:
    static constexpr const char* TAG = "light_engine";
    static constexpr int FRAME_MS = 16;

    Config cfg_{};
    led_strip_handle_t strip_ = nullptr;
    SemaphoreHandle_t mtx_ = nullptr;
    uint8_t gamma_[256]{};

    // per-channel output level 0..255 (brightness applied, gamma not yet)
    float from_[3]{}, to_[3]{}, cur_[3]{};
    int64_t t0_us_ = 0, dur_us_ = 0;

    static void task_entry(void*);
    void render_loop();
    void write_frame(const float level[3]);
};
