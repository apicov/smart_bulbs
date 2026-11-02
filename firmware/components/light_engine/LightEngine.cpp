#include "LightEngine.hpp"
#include <algorithm>
#include <cmath>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"

esp_err_t LightEngine::start(const Config& cfg) {
    cfg_ = cfg;

    for (int i = 0; i < 256; ++i)
        gamma_[i] = static_cast<uint8_t>(std::lround(std::pow(i / 255.0, 2.2) * 255.0));

    led_strip_config_t strip_cfg = {};
    strip_cfg.strip_gpio_num = cfg_.gpio;
    strip_cfg.max_leds = cfg_.led_count;
    strip_cfg.led_model = LED_MODEL_WS2812;
    strip_cfg.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;

    led_strip_rmt_config_t rmt_cfg = {};
    rmt_cfg.clk_src = RMT_CLK_SRC_DEFAULT;
    rmt_cfg.resolution_hz = 10 * 1000 * 1000;  // 10 MHz

    esp_err_t err = led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &strip_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "led_strip init failed: %s", esp_err_to_name(err));
        return err;
    }
    led_strip_clear(strip_);

    mtx_ = xSemaphoreCreateMutex();
    xTaskCreate(task_entry, "light", 4096, this, 10, nullptr);
    ESP_LOGI(TAG, "Started: %u LEDs on GPIO%d, limit %lu mA", cfg_.led_count, cfg_.gpio,
             (unsigned long)cfg_.max_milliamps);
    return ESP_OK;
}

void LightEngine::set(const LightState& s, uint32_t transition_ms) {
    const float k = s.on ? s.brightness / 255.0f : 0.0f;
    xSemaphoreTake(mtx_, portMAX_DELAY);
    for (int c = 0; c < 3; ++c) from_[c] = cur_[c];  // start from what's on screen now
    to_[0] = s.color.r * k;
    to_[1] = s.color.g * k;
    to_[2] = s.color.b * k;
    t0_us_ = esp_timer_get_time();
    dur_us_ = static_cast<int64_t>(transition_ms) * 1000;
    xSemaphoreGive(mtx_);
}

void LightEngine::task_entry(void* self) { static_cast<LightEngine*>(self)->render_loop(); }

void LightEngine::render_loop() {
    TickType_t last_wake = xTaskGetTickCount();
    float shown[3] = {-1, -1, -1};
    int64_t last_write_us = 0;

    for (;;) {
        float level[3];
        xSemaphoreTake(mtx_, portMAX_DELAY);
        const int64_t now = esp_timer_get_time();
        const float t = dur_us_ <= 0 ? 1.0f
                                     : std::min(1.0f, float(now - t0_us_) / float(dur_us_));
        for (int c = 0; c < 3; ++c) cur_[c] = level[c] = from_[c] + (to_[c] - from_[c]) * t;
        xSemaphoreGive(mtx_);

        // Only push to the strip when something changed, plus a 1 s refresh
        // so a glitched pixel (noise on the data line) recovers on its own.
        const bool changed = level[0] != shown[0] || level[1] != shown[1] || level[2] != shown[2];
        if (changed || now - last_write_us > 1000000) {
            write_frame(level);
            for (int c = 0; c < 3; ++c) shown[c] = level[c];
            last_write_us = now;
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(FRAME_MS));
    }
}

void LightEngine::write_frame(const float level[3]) {
    uint8_t out[3];
    for (int c = 0; c < 3; ++c)
        out[c] = gamma_[static_cast<uint8_t>(std::lround(std::clamp(level[c], 0.0f, 255.0f)))];

    // Current limit: estimate draw of the (uniform) frame and scale down if needed.
    if (cfg_.max_milliamps > 0) {
        const float ma = cfg_.led_count * cfg_.ma_per_channel * (out[0] + out[1] + out[2]) / 255.0f;
        if (ma > cfg_.max_milliamps) {
            const float s = cfg_.max_milliamps / ma;
            for (auto& v : out) v = static_cast<uint8_t>(v * s);
        }
    }

    for (uint32_t i = 0; i < cfg_.led_count; ++i) led_strip_set_pixel(strip_, i, out[0], out[1], out[2]);
    led_strip_refresh(strip_);
}
