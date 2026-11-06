#include "LightEngine.hpp"
#include <mutex>
#include <optional>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "led_strip_spi.h"

using std::chrono::microseconds;

namespace {
microseconds now() { return microseconds{esp_timer_get_time()}; }
}  // namespace

esp_err_t LightEngine::start(const Config& cfg) {
    led_count_ = cfg.led_count;
    power_ = {
        .led_count = cfg.led_count, .ma_per_channel = cfg.ma_per_channel, .max_milliamps = cfg.max_milliamps};

    led_strip_config_t strip_cfg = {};
    strip_cfg.strip_gpio_num = cfg.gpio;
    strip_cfg.max_leds = cfg.led_count;
    strip_cfg.led_model = LED_MODEL_WS2812;
    strip_cfg.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;

    // SPI with DMA, not RMT: a frame goes out in one transfer, so it's less exposed to
    // WiFi interrupts on this single-core chip. RMT has to refill its 48-symbol buffer
    // from an interrupt throughout the frame, and led_strip enables and disables the
    // channel around each frame, which has wedged under radio load on other chips.
    led_strip_spi_config_t spi_cfg = {};
    spi_cfg.clk_src = SPI_CLK_SRC_DEFAULT;
    spi_cfg.spi_bus = SPI2_HOST;
    spi_cfg.flags.with_dma = true;

    ESP_RETURN_ON_ERROR(led_strip_new_spi_device(&strip_cfg, &spi_cfg, &strip_), TAG,
                        "led_strip init failed");
    led_strip_clear(strip_);

    task_.start<&LightEngine::render_loop>("light", TASK_PRIORITY, this);
    ESP_LOGI(TAG, "Started: %u LEDs on GPIO%d, limit %lu mA", cfg.led_count, cfg.gpio,
             static_cast<unsigned long>(cfg.max_milliamps));
    return ESP_OK;
}

void LightEngine::set(const LightState& s, std::chrono::milliseconds transition) {
    const Levels target = target_levels(s);
    const std::lock_guard lock{mtx_};
    from_ = cur_;  // start from what's on screen now
    to_ = target;
    fade_start_ = now();
    fade_duration_ = transition;
}

void LightEngine::render_loop() {
    TickType_t last_wake = xTaskGetTickCount();
    std::optional<Rgb> shown;
    microseconds last_write{0};

    for (;;) {
        microseconds time;
        Levels levels;
        {
            const std::lock_guard lock{mtx_};
            time = now();
            levels = cur_ = fade_levels(from_, to_, time - fade_start_, fade_duration_);
        }

        // Only push to the strip when something changed, plus a periodic refresh
        // so a glitched pixel (noise on the data line) recovers on its own.
        const Rgb pixel = to_pixel(levels, power_);
        if (pixel != shown || time - last_write > REFRESH_PERIOD) {
            write_frame(pixel);
            shown = pixel;
            last_write = time;
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(FRAME_MS));
    }
}

void LightEngine::write_frame(const Rgb& pixel) {
    for (uint32_t i = 0; i < led_count_; ++i) led_strip_set_pixel(strip_, i, pixel.r, pixel.g, pixel.b);
    const esp_err_t err = led_strip_refresh(strip_);
    // A stuck strip fails every frame, so report only the first failure.
    if (err != ESP_OK && !refresh_failed_) ESP_LOGE(TAG, "strip refresh failed: %s", esp_err_to_name(err));
    refresh_failed_ = err != ESP_OK;
}
