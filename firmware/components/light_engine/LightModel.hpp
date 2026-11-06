#pragma once
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

// The light's logic, free of ESP-IDF so it can be unit tested on the host.
// Integer only at runtime: the ESP32-C6 has no FPU.

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

/** Per-channel output with brightness applied, before gamma. Scaled by 255: 0..65025. */
using Levels = std::array<uint16_t, 3>;

struct PowerBudget {
    uint32_t led_count;
    uint32_t ma_per_channel;
    uint32_t max_milliamps;  ///< 0 = no limit
};

namespace detail {
/** x^2.2 for x in 0..1, as x^2 * x^(1/5). std::pow isn't portably constexpr before C++26. */
consteval double pow_2_2(double x) {
    if (x <= 0.0) return 0.0;
    double fifth_root = 1.0;
    for (int i = 0; i < 100; ++i) {  // Newton's method on y^5 = x
        const double y2 = fifth_root * fifth_root;
        const double y4 = y2 * y2;
        fifth_root -= (y4 * fifth_root - x) / (5.0 * y4);
    }
    return x * x * fifth_root;
}

consteval std::array<uint8_t, 256> make_gamma_table() {
    std::array<uint8_t, 256> table{};
    for (std::size_t i = 0; i < table.size(); ++i)
        table[i] = static_cast<uint8_t>(pow_2_2(static_cast<double>(i) / 255.0) * 255.0 + 0.5);
    return table;
}
}  // namespace detail

/** Gamma 2.2, so brightness steps look even. Built at compile time and stored in flash. */
inline constexpr std::array<uint8_t, 256> GAMMA = detail::make_gamma_table();

constexpr Levels target_levels(const LightState& s) {
    const int k = s.on ? s.brightness : 0;
    return {static_cast<uint16_t>(s.color.r * k), static_cast<uint16_t>(s.color.g * k),
            static_cast<uint16_t>(s.color.b * k)};
}

/** Linear fade from `from` to `to`, `elapsed` into a fade lasting `duration`. */
constexpr Levels fade_levels(const Levels& from, const Levels& to, std::chrono::microseconds elapsed,
                             std::chrono::microseconds duration) {
    if (duration.count() <= 0 || elapsed >= duration) return to;
    Levels out{};
    for (std::size_t c = 0; c < out.size(); ++c) {
        const int64_t delta = int64_t{to[c]} - int64_t{from[c]};
        out[c] = static_cast<uint16_t>(from[c] + delta * elapsed.count() / duration.count());
    }
    return out;
}

/** Levels to strip values: gamma correct, then scale down to stay within the current budget. */
constexpr Rgb to_pixel(const Levels& levels, const PowerBudget& power) {
    std::array<uint32_t, 3> out{};
    for (std::size_t c = 0; c < out.size(); ++c)
        out[c] = GAMMA[(levels[c] + 127u) / 255u];  // rounds to the nearest 0..255

    if (power.max_milliamps > 0) {
        // Both sides stay scaled by 255 to keep the sums exact.
        const uint64_t estimate =
            uint64_t{power.led_count} * power.ma_per_channel * (out[0] + out[1] + out[2]);
        const uint64_t limit = uint64_t{power.max_milliamps} * 255u;
        if (estimate > limit)
            for (auto& v : out) v = static_cast<uint32_t>(v * limit / estimate);
    }
    return {static_cast<uint8_t>(out[0]), static_cast<uint8_t>(out[1]), static_cast<uint8_t>(out[2])};
}
