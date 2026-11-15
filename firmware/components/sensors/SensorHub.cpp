#include "SensorHub.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr const char* TAG = "sensor_hub";
constexpr uint32_t SCD30_BOOT_MS = 2000;  // it ignores I2C for 2 s after power-up

/** Log when a sensor starts or stops failing, rather than on every read. */
template <class T>
std::optional<T> keep(std::expected<T, esp_err_t> result, const char* name, bool& failing) {
    if (result) {
        if (failing) ESP_LOGI(TAG, "%s reads again", name);
        failing = false;
        return *std::move(result);
    }
    if (!failing) ESP_LOGW(TAG, "%s read failed: %s", name, esp_err_to_name(result.error()));
    failing = true;
    return std::nullopt;
}

bool started(esp_err_t err, const char* name) {
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "%s found", name);
        return true;
    }
    ESP_LOGW(TAG, "%s not used: %s", name, esp_err_to_name(err));
    return false;
}
}  // namespace

void SensorHub::start(const Config& cfg, Publish publish) {
    cfg_ = cfg;
    publish_ = publish;
    task_.start<&SensorHub::run>("sensors", TASK_PRIORITY, this);
}

void SensorHub::start_sensors() {
    const auto bus = sensors::open_bus(cfg_.pins);
    if (!bus) {
        ESP_LOGE(TAG, "I2C bus failed: %s. No sensors.", esp_err_to_name(bus.error()));
        return;
    }

    // First, so the TSL2591 has 0x29 to itself by the time it starts.
    if (cfg_.vl53l1x_xshut != GPIO_NUM_NC)
        has_vl53l1x_ = started(vl53l1x_.start(*bus, cfg_.vl53l1x_xshut, cfg_.vl53l1x_address), "VL53L1X");
    else
        ESP_LOGI(TAG, "VL53L1X not used: no XSHUT pin, so it can't move off the TSL2591's address");

    has_tsl2591_ = started(tsl2591_.start(cfg_.pins), "TSL2591");
    has_veml7700_ = started(veml7700_.start(cfg_.pins), "VEML7700");
    has_scd30_ = started(scd30_.start(cfg_.pins, cfg_.period), "SCD30");
}

SensorReadings SensorHub::read_all() {
    SensorReadings out;
    if (has_veml7700_) out.veml7700_lux = keep(veml7700_.read_lux(), "VEML7700", failing_.veml7700);
    if (has_tsl2591_) out.tsl2591_lux = keep(tsl2591_.read_lux(), "TSL2591", failing_.tsl2591);
    if (has_vl53l1x_) out.vl53l1x = keep(vl53l1x_.read(), "VL53L1X", failing_.vl53l1x);
    if (has_scd30_) {
        if (const auto fresh = keep(scd30_.read(), "SCD30", failing_.scd30); !fresh)
            last_air_.reset();  // a failing sensor's old reading is no longer true
        else if (*fresh)
            last_air_ = **fresh;
        out.scd30 = last_air_;
    }
    return out;
}

void SensorHub::run() {
    vTaskDelay(pdMS_TO_TICKS(SCD30_BOOT_MS));
    start_sensors();

    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(cfg_.period.count() * 1000);
    for (;;) {
        const SensorsJson json = format_sensors(read_all(), camera_.load());
        ESP_LOGD(TAG, "%.*s", static_cast<int>(json.view().size()), json.view().data());
        publish_(json.view());
        vTaskDelayUntil(&last_wake, period);
    }
}
