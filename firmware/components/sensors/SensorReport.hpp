#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

// The `sensors` message, free of ESP-IDF so it can be unit tested on the host.
//
//   sensors: {"veml7700":{"lux":123.4},"tsl2591":{"lux":118.0},"vl53l1x":{"mm":530},
//             "scd30":{"co2":612,"temp":22.4,"rh":41.0},"camera":"ok"}
//
// A sensor that isn't fitted or doesn't answer is left out. "mm" is null when the
// distance sensor sees nothing in range. "camera" is left out on boards without one.

struct Distance {
    bool valid;   ///< false: nothing in range, or too weak a return to trust
    uint16_t mm;  ///< meaningful only when valid
    bool operator==(const Distance&) const = default;
};

struct AirReading {
    float co2_ppm;
    float temp_c;
    float humidity_pct;
};

struct SensorReadings {
    std::optional<float> veml7700_lux;
    std::optional<float> tsl2591_lux;
    std::optional<Distance> vl53l1x;
    std::optional<AirReading> scd30;
};

/** A formatted sensors message, big enough for the longest possible one. */
class SensorsJson {
public:
    [[nodiscard]] std::string_view view() const { return {buf_.data(), len_}; }

private:
    friend SensorsJson format_sensors(const SensorReadings& readings, std::string_view camera);
    std::array<char, 256> buf_{};
    std::size_t len_ = 0;
};

/**
 * `camera` is a short status such as "ok", or empty to leave it out.
 * Readings outside the sensor's range are left out.
 */
[[nodiscard]] SensorsJson format_sensors(const SensorReadings& readings, std::string_view camera);
