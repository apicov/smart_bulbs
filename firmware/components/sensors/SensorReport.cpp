#include "SensorReport.hpp"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace {
/** Appends to a fixed buffer, and stops at its end instead of overflowing. */
class Writer {
public:
    Writer(char* buf, std::size_t size) : buf_(buf), size_(size) {}

    // NOLINTNEXTLINE(cert-dcl50-cpp): a printf-style helper, checked by the format attribute
    __attribute__((format(printf, 2, 3))) void add(const char* fmt, ...) {
        if (len_ >= size_) return;
        va_list args;
        va_start(args, fmt);
        const int n = std::vsnprintf(buf_ + len_, size_ - len_, fmt, args);
        va_end(args);
        len_ = n < 0 ? size_ : std::min(size_, len_ + static_cast<std::size_t>(n));
    }

    /** Starts the next member: a comma after the first one. */
    void key(const char* name) {
        add(first_ ? R"("%s":)" : R"(,"%s":)", name);
        first_ = false;
    }

    [[nodiscard]] std::size_t length() const { return std::min(len_, size_ - 1); }

private:
    char* buf_;
    std::size_t size_;
    std::size_t len_ = 0;
    bool first_ = true;
};

/** Outside these a reading is a fault, and leaving it out also bounds the message length. */
bool within(float v, float lo, float hi) { return std::isfinite(v) && v >= lo && v <= hi; }
constexpr float MAX_LUX = 200000.0F;  // brighter than direct sunlight
}  // namespace

SensorsJson format_sensors(const SensorReadings& readings, std::string_view camera) {
    SensorsJson out;
    Writer w{out.buf_.data(), out.buf_.size()};
    w.add("{");
    if (readings.veml7700_lux && within(*readings.veml7700_lux, 0.0F, MAX_LUX)) {
        w.key("veml7700");
        w.add(R"({"lux":%.1f})", static_cast<double>(*readings.veml7700_lux));
    }
    if (readings.tsl2591_lux && within(*readings.tsl2591_lux, 0.0F, MAX_LUX)) {
        w.key("tsl2591");
        w.add(R"({"lux":%.1f})", static_cast<double>(*readings.tsl2591_lux));
    }
    if (readings.vl53l1x) {
        w.key("vl53l1x");
        if (readings.vl53l1x->valid)
            w.add(R"({"mm":%u})", static_cast<unsigned>(readings.vl53l1x->mm));
        else
            w.add(R"({"mm":null})");
    }
    if (const auto& air = readings.scd30;
        air && within(air->co2_ppm, 0.0F, 40000.0F) && within(air->temp_c, -40.0F, 70.0F) &&
        within(air->humidity_pct, 0.0F, 100.0F)) {  // the SCD30's measuring ranges
        w.key("scd30");
        w.add(R"({"co2":%.0f,"temp":%.1f,"rh":%.1f})", static_cast<double>(air->co2_ppm),
              static_cast<double>(air->temp_c), static_cast<double>(air->humidity_pct));
    }
    if (!camera.empty()) {
        w.key("camera");
        // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage): %.*s is given the length
        w.add(R"("%.*s")", static_cast<int>(std::min<std::size_t>(camera.size(), 32)), camera.data());
    }
    w.add("}");
    out.len_ = w.length();
    return out;
}
