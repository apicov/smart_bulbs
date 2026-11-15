#pragma once
#include <atomic>
#include <chrono>
#include <cstddef>
#include <string_view>

#include "Callback.hpp"
#include "Sensors.hpp"
#include "StaticTask.hpp"

/**
 * Reads every I2C sensor that answered at boot and reports them together.
 *
 * Sensors start in the hub's own task, so a slow or missing one never holds up
 * the lamp. A missing sensor is logged once and left out of the report.
 */
class SensorHub {
public:
    struct Config {
        sensors::I2cPins pins;
        gpio_num_t vl53l1x_xshut;  ///< GPIO_NUM_NC: no VL53L1X fitted
        uint8_t vl53l1x_address;   ///< anything but 0x29, which the TSL2591 needs
        std::chrono::seconds period;
    };
    using Publish = Callback<void(std::string_view json)>;

    SensorHub() = default;
    SensorHub(const SensorHub&) = delete;
    SensorHub& operator=(const SensorHub&) = delete;

    void start(const Config& cfg, Publish publish);

    /** Included in each report. Must point to a string that lives forever. Thread-safe. */
    void set_camera_status(const char* status) { camera_.store(status); }

    [[nodiscard]] std::size_t stack_free_bytes() const { return task_.stack_free_bytes(); }

private:
    static constexpr std::size_t TASK_STACK_BYTES = 4096;
    static constexpr UBaseType_t TASK_PRIORITY = 3;  // below the light, which must never stutter

    [[noreturn]] void run();
    void start_sensors();
    [[nodiscard]] SensorReadings read_all();

    Config cfg_{};
    Publish publish_;
    std::atomic<const char*> camera_{""};

    sensors::Veml7700 veml7700_;
    sensors::Tsl2591 tsl2591_;
    sensors::Scd30 scd30_;
    sensors::Vl53l1x vl53l1x_;
    bool has_veml7700_ = false, has_tsl2591_ = false, has_scd30_ = false, has_vl53l1x_ = false;
    struct {
        bool veml7700 = false, tsl2591 = false, scd30 = false, vl53l1x = false;
    } failing_;                           ///< so each sensor's problem is logged once, not on every read
    std::optional<AirReading> last_air_;  ///< the SCD30 only has news once per period

    rtos::StaticTask<TASK_STACK_BYTES> task_;
};
