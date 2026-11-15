#pragma once
#include <chrono>
#include <cstdint>
#include <expected>
#include <optional>

#include "SensorReport.hpp"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "i2cdev.h"
#include "scd30.h"
#include "tsl2591.h"
#include "veml7700.h"
// vl53l1x.h includes ST's header outside its own C++ guard, and ST's has none: include it first.
extern "C" {
#include "VL53L1X_ULP_api.h"
}
#include "vl53l1x.h"

/**
 * C++ wrappers for the I2C sensors, over the maintained esp-idf-lib drivers
 * (VEML7700, TSL2591, SCD30) and grrtzm/vl53l1x_library (ST's VL53L1X ULP API, vendored
 * in ../vl53l1x_library with bounded I2C timeouts).
 *
 * All share one bus, which esp-idf-lib's i2cdev owns. Each wrapper keeps its
 * driver's device descriptor inside itself, and i2cdev holds a pointer to it,
 * so they can't be copied or moved: start() them where they will stay.
 */
namespace sensors {

struct I2cPins {
    i2c_port_t port;
    gpio_num_t sda;
    gpio_num_t scl;
};

/** Install the shared bus and return its handle, for drivers that take one directly. */
[[nodiscard]] std::expected<i2c_master_bus_handle_t, esp_err_t> open_bus(const I2cPins& pins);

class Veml7700 {
public:
    Veml7700() = default;
    Veml7700(const Veml7700&) = delete;
    Veml7700& operator=(const Veml7700&) = delete;
    ~Veml7700();

    /** Gain 1/4 and 100 ms: 0.23 lux steps, up to about 15000 lux, enough indoors. */
    [[nodiscard]] esp_err_t start(const I2cPins& pins);
    [[nodiscard]] std::expected<float, esp_err_t> read_lux();

private:
    i2c_dev_t dev_{};
    veml7700_config_t config_{};
    bool started_ = false;
};

class Tsl2591 {
public:
    Tsl2591() = default;
    Tsl2591(const Tsl2591&) = delete;
    Tsl2591& operator=(const Tsl2591&) = delete;
    ~Tsl2591();

    /** Its address is fixed at 0x29, so start it after the VL53L1X has moved off it. */
    [[nodiscard]] esp_err_t start(const I2cPins& pins);
    [[nodiscard]] std::expected<float, esp_err_t> read_lux();

private:
    tsl2591_t dev_{};
    bool started_ = false;
};

class Scd30 {
public:
    Scd30() = default;
    Scd30(const Scd30&) = delete;
    Scd30& operator=(const Scd30&) = delete;
    ~Scd30();

    /** Measures continuously, once per `interval`, with automatic self-calibration. */
    [[nodiscard]] esp_err_t start(const I2cPins& pins, std::chrono::seconds interval);
    /** The latest measurement, or nullopt if none has finished since the last read. */
    [[nodiscard]] std::expected<std::optional<AirReading>, esp_err_t> read();

private:
    i2c_dev_t dev_{};
    bool started_ = false;
};

class Vl53l1x {
public:
    static constexpr uint8_t DEFAULT_ADDRESS = 0x29;

    Vl53l1x() = default;
    Vl53l1x(const Vl53l1x&) = delete;
    Vl53l1x& operator=(const Vl53l1x&) = delete;
    ~Vl53l1x();

    /**
     * Reset it with XSHUT, move it from 0x29 to `address` and start ranging every 100 ms.
     * The new address lasts until it loses power, so this runs on every boot.
     */
    [[nodiscard]] esp_err_t start(i2c_master_bus_handle_t bus, gpio_num_t xshut, uint8_t address);
    [[nodiscard]] std::expected<Distance, esp_err_t> read();

private:
    vl53l1x_t dev_{};
    bool started_ = false;
};

}  // namespace sensors
