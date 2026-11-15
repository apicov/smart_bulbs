#include "Camera.hpp"
#include "esp_check.h"
#include "esp_log.h"
#include "sensor.h"

namespace {
constexpr const char* TAG = "camera";

// Seeed XIAO ESP32-S3 Sense, camera on the expansion board's connector.
// It has no power-down, reset or flash pins.
constexpr int PIN_XCLK = 10;
constexpr int PIN_SCCB_SDA = 40;
constexpr int PIN_SCCB_SCL = 39;
constexpr int PIN_D7 = 48, PIN_D6 = 11, PIN_D5 = 12, PIN_D4 = 14;
constexpr int PIN_D3 = 16, PIN_D2 = 18, PIN_D1 = 17, PIN_D0 = 15;
constexpr int PIN_VSYNC = 38, PIN_HREF = 47, PIN_PCLK = 13;
constexpr int XCLK_HZ = 20'000'000;
}  // namespace

esp_err_t Camera::start(const Config& cfg) {
    ESP_RETURN_ON_FALSE(!started_, ESP_ERR_INVALID_STATE, TAG, "already started");

    // Set field by field: the struct has anonymous unions, which designated initializers can't name.
    camera_config_t config{};
    config.pin_pwdn = -1;
    config.pin_reset = -1;  // the driver resets the sensor over SCCB instead
    config.pin_xclk = PIN_XCLK;
    config.pin_sccb_sda = PIN_SCCB_SDA;
    config.pin_sccb_scl = PIN_SCCB_SCL;
    config.pin_d7 = PIN_D7;
    config.pin_d6 = PIN_D6;
    config.pin_d5 = PIN_D5;
    config.pin_d4 = PIN_D4;
    config.pin_d3 = PIN_D3;
    config.pin_d2 = PIN_D2;
    config.pin_d1 = PIN_D1;
    config.pin_d0 = PIN_D0;
    config.pin_vsync = PIN_VSYNC;
    config.pin_href = PIN_HREF;
    config.pin_pclk = PIN_PCLK;
    config.xclk_freq_hz = XCLK_HZ;
    config.ledc_timer = LEDC_TIMER_0;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.pixel_format = PIXFORMAT_JPEG;
    config.frame_size = cfg.frame_size;
    config.jpeg_quality = cfg.jpeg_quality;
    config.fb_count = 2;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_LATEST;
    // With SCCB pins given, the I2C port comes from Kconfig: port 1, leaving 0 to the sensors.

    ESP_RETURN_ON_ERROR(esp_camera_init(&config), TAG, "no camera found");
    started_ = true;
    ESP_LOGI(TAG, "%s started", sensor_name());
    return ESP_OK;
}

// NOLINTNEXTLINE(readability-make-member-function-const): it takes a buffer from the driver
std::expected<Camera::Frame, esp_err_t> Camera::capture() {
    if (!started_) return std::unexpected{ESP_ERR_INVALID_STATE};
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb == nullptr) return std::unexpected{ESP_ERR_TIMEOUT};
    return Frame{fb};
}

const char* Camera::sensor_name() const {
    if (!started_) return "none";
    sensor_t* sensor = esp_camera_sensor_get();
    if (sensor == nullptr) return "unknown";
    const camera_sensor_info_t* info = esp_camera_sensor_get_info(&sensor->id);
    return info != nullptr ? info->name : "unknown";
}
