#pragma once
#include <cstdint>
#include <optional>
#include "LightModel.hpp"

// Sleep timer and wake-up alarm logic, free of ESP-IDF so it can be unit tested on the host.

inline constexpr uint32_t SECONDS_PER_DAY = 24 * 3600;
inline constexpr uint32_t SECONDS_PER_WEEK = 7 * SECONDS_PER_DAY;

/** The light is fully off `after_s` from now, having faded for the last `fade_s` of that. */
struct SleepTimer {
    uint32_t after_s;
    uint32_t fade_s;  ///< 0 = sudden. Never more than after_s.
};

/** A wake-up alarm. The light reaches `brightness` at hour:minute, starting `rise_s` earlier. */
struct Alarm {
    bool enabled = false;
    uint8_t hour = 7;
    uint8_t minute = 0;
    uint8_t days = 0;  ///< Bit 0 = Sunday .. bit 6 = Saturday. 0 = once, at the next hour:minute.
    uint16_t rise_s = 1800;
    uint8_t brightness = 255;
    Rgb color{255, 180, 100};
    bool operator==(const Alarm&) const = default;
};

/** Seconds since Sunday 00:00. `weekday` is 0 for Sunday, as in `struct tm`. */
constexpr uint32_t second_of_week(int weekday, int hour, int minute, int second) {
    return (static_cast<uint32_t>(weekday) * SECONDS_PER_DAY) + (static_cast<uint32_t>(hour) * 3600) +
           (static_cast<uint32_t>(minute) * 60) + static_cast<uint32_t>(second);
}

/**
 * Seconds until the alarm time, if `now` is inside a rise. Empty otherwise.
 * Covers rises that cross midnight or the end of the week. Being stateless, it
 * also lets a bulb that was restarted mid-rise join in with the time left.
 */
constexpr std::optional<uint32_t> alarm_rise_remaining(const Alarm& alarm, uint32_t now) {
    if (!alarm.enabled) return std::nullopt;
    for (int day = 0; day < 7; ++day) {
        if (alarm.days != 0 && (alarm.days & (1U << day)) == 0) continue;
        const uint32_t at = second_of_week(day, alarm.hour, alarm.minute, 0);
        const uint32_t remaining = (at + SECONDS_PER_WEEK - now) % SECONDS_PER_WEEK;
        if (remaining > 0 && remaining <= alarm.rise_s) return remaining;
    }
    return std::nullopt;
}
