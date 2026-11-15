#include "Sensors.hpp"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace sensors {
namespace {
constexpr const char* TAG = "sensors";

constexpr uint16_t VL53L1X_MODEL_ID = 0xEACC;

void sleep_ms(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

/** The ST API names a device by its 8-bit address. */
constexpr uint16_t st_address(uint8_t addr7) { return static_cast<uint16_t>(addr7 << 1); }
}  // namespace

std::expected<i2c_master_bus_handle_t, esp_err_t> open_bus(const I2cPins& pins) {
    if (const esp_err_t err = i2cdev_init(); err != ESP_OK) return std::unexpected{err};

    // i2cdev installs a port the first time a device on it is used. A presence check
    // does that without touching any device, whether or not one answers at this address.
    i2c_dev_t opener{};
    opener.port = pins.port;
    opener.addr = VEML7700_I2C_ADDR;
    opener.cfg.sda_io_num = pins.sda;
    opener.cfg.scl_io_num = pins.scl;
    opener.cfg.sda_pullup_en = 1;  // weak, but enough with short wires and no breakout pull-ups
    opener.cfg.scl_pullup_en = 1;
    opener.cfg.master.clk_speed = 100000;
    (void)i2c_dev_check_present(&opener);

    void* handle = nullptr;
    if (const esp_err_t err = i2cdev_get_shared_handle(pins.port, &handle); err != ESP_OK)
        return std::unexpected{err};
    return static_cast<i2c_master_bus_handle_t>(handle);
}

// ---------------------------------------------------------------- VEML7700

Veml7700::~Veml7700() {
    if (started_) (void)veml7700_free_desc(&dev_);
}

esp_err_t Veml7700::start(const I2cPins& pins) {
    ESP_RETURN_ON_ERROR(veml7700_init_desc(&dev_, pins.port, pins.sda, pins.scl), TAG, "VEML7700 descriptor");
    started_ = true;
    ESP_RETURN_ON_ERROR(veml7700_probe(&dev_), TAG, "VEML7700 not found");
    config_.gain = VEML7700_GAIN_DIV_4;
    config_.integration_time = VEML7700_INTEGRATION_TIME_100MS;
    return veml7700_set_config(&dev_, &config_);
}

std::expected<float, esp_err_t> Veml7700::read_lux() {
    uint32_t lux = 0;
    if (const esp_err_t err = veml7700_get_ambient_light(&dev_, &config_, &lux); err != ESP_OK)
        return std::unexpected{err};
    return static_cast<float>(lux);
}

// ---------------------------------------------------------------- TSL2591

Tsl2591::~Tsl2591() {
    if (started_) (void)tsl2591_free_desc(&dev_);
}

esp_err_t Tsl2591::start(const I2cPins& pins) {
    ESP_RETURN_ON_ERROR(tsl2591_init_desc(&dev_, pins.port, pins.sda, pins.scl), TAG, "TSL2591 descriptor");
    started_ = true;
    return tsl2591_init(&dev_);  // powered on, low gain, 100 ms
}

std::expected<float, esp_err_t> Tsl2591::read_lux() {
    float lux = 0.0F;
    if (const esp_err_t err = tsl2591_get_lux(&dev_, &lux); err != ESP_OK) return std::unexpected{err};
    return lux;
}

// ---------------------------------------------------------------- SCD30

Scd30::~Scd30() {
    if (started_) (void)scd30_free_desc(&dev_);
}

esp_err_t Scd30::start(const I2cPins& pins, std::chrono::seconds interval) {
    ESP_RETURN_ON_ERROR(scd30_init_desc(&dev_, pins.port, pins.sda, pins.scl), TAG, "SCD30 descriptor");
    started_ = true;
    ESP_RETURN_ON_ERROR(scd30_set_measurement_interval(&dev_, static_cast<uint16_t>(interval.count())), TAG,
                        "SCD30 not found");
    return scd30_trigger_continuous_measurement(&dev_, 0);  // 0: no pressure compensation
}

std::expected<std::optional<AirReading>, esp_err_t> Scd30::read() {
    bool ready = false;
    if (const esp_err_t err = scd30_get_data_ready_status(&dev_, &ready); err != ESP_OK)
        return std::unexpected{err};
    if (!ready) return std::optional<AirReading>{};

    AirReading air{};
    if (const esp_err_t err = scd30_read_measurement(&dev_, &air.co2_ppm, &air.temp_c, &air.humidity_pct);
        err != ESP_OK)
        return std::unexpected{err};
    return air;
}

// ---------------------------------------------------------------- VL53L1X

Vl53l1x::~Vl53l1x() {
    if (started_) (void)vl53l1x_deinit(&dev_);
}

esp_err_t Vl53l1x::start(i2c_master_bus_handle_t bus, gpio_num_t xshut, uint8_t address) {
    // Hold it in reset, then let it boot at its default address.
    const gpio_config_t io{.pin_bit_mask = 1ULL << xshut,
                           .mode = GPIO_MODE_OUTPUT,
                           .pull_up_en = GPIO_PULLUP_DISABLE,
                           .pull_down_en = GPIO_PULLDOWN_DISABLE,
                           .intr_type = GPIO_INTR_DISABLE};
    ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "XSHUT pin");
    ESP_RETURN_ON_ERROR(gpio_set_level(xshut, 0), TAG, "XSHUT low");
    sleep_ms(10);
    ESP_RETURN_ON_ERROR(gpio_set_level(xshut, 1), TAG, "XSHUT high");
    sleep_ms(2);  // boots in 1.2 ms

    // At 0x29 it shares the address with the TSL2591, so only write to it there: two
    // devices answering a read would garble the reply. The TSL2591 ignores this write,
    // because it only accepts bytes with its command bit (0x80) set.
    ESP_RETURN_ON_ERROR(vl53l1x_init(&dev_, bus, DEFAULT_ADDRESS), TAG, "VL53L1X not found");
    started_ = true;
    ESP_RETURN_ON_FALSE(address <= 0x7F, ESP_ERR_INVALID_ARG, TAG, "VL53L1X address must be 7-bit");
    const uint8_t moved =
        VL53L1X_ULP_SetI2CAddress(st_address(DEFAULT_ADDRESS), static_cast<uint8_t>(st_address(address)));
    (void)vl53l1x_deinit(&dev_);
    started_ = false;
    ESP_RETURN_ON_FALSE(moved == 0, ESP_FAIL, TAG, "VL53L1X address change failed");

    ESP_RETURN_ON_ERROR(vl53l1x_init(&dev_, bus, address), TAG, "VL53L1X not found at its new address");
    started_ = true;
    uint16_t id = 0;
    ESP_RETURN_ON_ERROR(vl53l1x_get_sensor_id(&dev_, &id), TAG, "VL53L1X id");
    ESP_RETURN_ON_FALSE(id == VL53L1X_MODEL_ID, ESP_ERR_NOT_FOUND, TAG, "0x%02x isn't a VL53L1X (id 0x%04x)",
                        address, id);
    ESP_RETURN_ON_ERROR(vl53l1x_sensor_init(&dev_), TAG, "VL53L1X init");
    ESP_RETURN_ON_ERROR(vl53l1x_config_long_100ms(&dev_), TAG, "VL53L1X timing");  // up to about 1.3 m
    return vl53l1x_start(&dev_);
}

std::expected<Distance, esp_err_t> Vl53l1x::read() {
    vl53l1x_result_t result{};
    // Waits up to one ranging period for a result.
    if (const esp_err_t err = vl53l1x_read(&dev_, &result, 150); err != ESP_OK) return std::unexpected{err};
    return Distance{.valid = result.status == 0, .mm = result.distance_mm};
}

}  // namespace sensors
