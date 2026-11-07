#pragma once
#include <array>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string_view>

// The bulb's identity and secrets. tools/provision.py writes them to the `bulbcfg`
// partition, so the firmware image holds none and can be shared by every bulb.

struct BulbConfig {
    std::array<char, 33> id{};  ///< MQTT username and topic segment
    std::array<char, 33> wifi_ssid{};
    std::array<char, 65> wifi_password{};
    std::array<char, 128> mqtt_uri{};
    std::array<char, 65> mqtt_password{};
    uint32_t max_milliamps = 0;
    uint16_t led_count = 0;
};

/** What's wrong with `config`, or nullptr if it's usable. Never mentions secret values. */
inline const char* config_problem(const BulbConfig& config) {
    const std::string_view id{config.id.data()};
    if (id.empty()) return "missing id";
    for (const char c : id) {
        const auto u = static_cast<unsigned char>(c);
        if (std::isdigit(u) == 0 && (u < 'a' || u > 'z') && c != '-')
            return "id must use only a-z, 0-9 and -";
    }
    if (config.wifi_ssid[0] == '\0') return "missing WiFi SSID";
    if (std::string_view{config.wifi_password.data()}.size() < 8)
        return "WiFi password is under 8 characters";

    const std::string_view uri{config.mqtt_uri.data()};
    if (!uri.starts_with("mqtt://") && !uri.starts_with("mqtts://"))
        return "MQTT URI must start with mqtt(s)://";
    if (config.mqtt_password[0] == '\0') return "missing MQTT password";
    if (config.led_count < 1 || config.led_count > 1024) return "LED count must be 1 to 1024";
    return nullptr;
}

/**
 * Read and validate the `bulbcfg` partition. Fields other than the numbers are
 * required, and `max_milliamps` and `led_count` fall back to the given defaults.
 * Empty if the bulb isn't provisioned or its settings aren't usable.
 */
[[nodiscard]] std::optional<BulbConfig> load_bulb_config(uint32_t default_max_milliamps,
                                                         uint16_t default_led_count);
