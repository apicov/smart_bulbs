#include "BulbConfig.hpp"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace {
constexpr const char* TAG = "bulb_config";
constexpr const char* PARTITION = "bulbcfg";
constexpr const char* NAMESPACE = "bulb";

/** Scoped read-only handle on the config partition. */
class ConfigHandle {
public:
    ConfigHandle() : err_{nvs_open_from_partition(PARTITION, NAMESPACE, NVS_READONLY, &handle_)} {}
    ~ConfigHandle() {
        if (err_ == ESP_OK) nvs_close(handle_);
    }
    ConfigHandle(const ConfigHandle&) = delete;
    ConfigHandle& operator=(const ConfigHandle&) = delete;

    [[nodiscard]] esp_err_t error() const { return err_; }
    [[nodiscard]] nvs_handle_t get() const { return handle_; }

private:
    nvs_handle_t handle_ = 0;
    esp_err_t err_;
};

/** A value too long for its field fails to read, rather than being cut short. */
template <std::size_t N>
bool read_string(const ConfigHandle& nvs, const char* key, std::array<char, N>& out) {
    std::size_t size = out.size();
    const esp_err_t err = nvs_get_str(nvs.get(), key, out.data(), &size);
    if (err != ESP_OK) ESP_LOGE(TAG, "Can't read %s: %s", key, esp_err_to_name(err));
    return err == ESP_OK;
}
}  // namespace

std::optional<BulbConfig> load_bulb_config(uint32_t default_max_milliamps, uint16_t default_led_count) {
    // Never erased here, unlike the `nvs` partition: without these settings a
    // sealed bulb could never come back online.
    const esp_err_t init_err = nvs_flash_init_partition(PARTITION);
    if (init_err != ESP_OK) {
        ESP_LOGE(TAG, "Can't open the %s partition: %s", PARTITION, esp_err_to_name(init_err));
        return std::nullopt;
    }

    const ConfigHandle nvs;
    if (nvs.error() == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Not provisioned: run tools/provision.py");
        return std::nullopt;
    }
    if (nvs.error() != ESP_OK) {
        ESP_LOGE(TAG, "Can't open settings: %s", esp_err_to_name(nvs.error()));
        return std::nullopt;
    }

    BulbConfig config;
    if (!read_string(nvs, "id", config.id) || !read_string(nvs, "wifi_ssid", config.wifi_ssid) ||
        !read_string(nvs, "wifi_pass", config.wifi_password) ||
        !read_string(nvs, "mqtt_uri", config.mqtt_uri) ||
        !read_string(nvs, "mqtt_pass", config.mqtt_password)) {
        return std::nullopt;
    }
    if (nvs_get_u32(nvs.get(), "max_ma", &config.max_milliamps) != ESP_OK)
        config.max_milliamps = default_max_milliamps;
    if (nvs_get_u16(nvs.get(), "led_count", &config.led_count) != ESP_OK)
        config.led_count = default_led_count;

    if (const char* problem = config_problem(config)) {
        ESP_LOGE(TAG, "Unusable settings: %s", problem);
        return std::nullopt;
    }
    return config;
}
