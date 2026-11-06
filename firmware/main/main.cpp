#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include <esp_log.h>
#include <esp_mac.h>
#include <nvs_flash.h>

#include <BulbCtrl.hpp>
#include <LightEngine.hpp>
#include <MQTTClient.hpp>
#include <Pir.hpp>
#include <WiFiStation.hpp>

namespace {
constexpr const char* TAG = "main";

#ifdef CONFIG_BULB_POWER_ON_LIGHT
constexpr bool POWER_ON_LIGHT = true;
#else
constexpr bool POWER_ON_LIGHT = false;
#endif

// A bad configuration should fail the build, not misbehave on the device.
static_assert(sizeof(CONFIG_BULB_WIFI_SSID) <= sizeof(wifi_sta_config_t::ssid), "WiFi SSID too long");
static_assert(sizeof(CONFIG_BULB_WIFI_PASSWORD) <= sizeof(wifi_sta_config_t::password),
              "WiFi password too long");
static_assert(CONFIG_BULB_MAX_MILLIAMPS >= 0, "current limit can't be negative");

// sizeof includes each string's NUL, which leaves room for the separators too,
// so topics built from these can't be truncated.
constexpr std::size_t ID_CAPACITY = std::max(sizeof(CONFIG_BULB_ID), sizeof("bulb-xxxxxx"));
constexpr std::size_t TOPIC_CAPACITY = sizeof(CONFIG_BULB_TOPIC_PREFIX) + ID_CAPACITY + sizeof("status");

using BulbId = std::array<char, ID_CAPACITY>;
using Topic = std::array<char, TOPIC_CAPACITY>;

BulbId make_bulb_id() {
    BulbId id{};
    if constexpr (sizeof(CONFIG_BULB_ID) > 1) {
        (void)std::snprintf(id.data(), id.size(), "%s", CONFIG_BULB_ID);
    } else {
        std::array<uint8_t, 6> mac{};
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_read_mac(mac.data(), ESP_MAC_WIFI_STA));
        (void)std::snprintf(id.data(), id.size(), "bulb-%02x%02x%02x", mac[3], mac[4], mac[5]);
    }
    return id;
}

Topic make_topic(const BulbId& id, const char* leaf) {
    Topic topic{};
    (void)std::snprintf(topic.data(), topic.size(), "%s/%s/%s", CONFIG_BULB_TOPIC_PREFIX, id.data(), leaf);
    return topic;
}

constexpr std::string_view motion_json(bool motion) {
    return motion ? R"({"motion":true})" : R"({"motion":false})";
}

/** Owns every part of the bulb and wires them together. */
class App {
public:
    void run() {
        ESP_LOGI(TAG, "Bulb id: %s (topics %s/%s/*)", id_.data(), CONFIG_BULB_TOPIC_PREFIX, id_.data());

        // Light first, so the bulb works as a plain lamp even without WiFi.
        ESP_ERROR_CHECK(engine_.start({.gpio = CONFIG_BULB_LED_GPIO,
                                       .led_count = CONFIG_BULB_LED_COUNT,
                                       .max_milliamps = CONFIG_BULB_MAX_MILLIAMPS}));
        ESP_ERROR_CHECK(ctrl_.start(POWER_ON_LIGHT, [this](std::string_view json) { publish_state(json); }));

        // Past this point a failure costs a feature, not the lamp: log it and carry on.
        if constexpr (CONFIG_BULB_PIR_GPIO >= 0) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(pir_.start(static_cast<gpio_num_t>(CONFIG_BULB_PIR_GPIO),
                                                     [this](bool motion) { publish_motion(motion); }));
        }
        ESP_ERROR_CHECK_WITHOUT_ABORT(
            wifi_.start(CONFIG_BULB_WIFI_SSID, CONFIG_BULB_WIFI_PASSWORD, [this] { on_wifi_connected(); }));

        ESP_LOGI(TAG, "pico-bulb is running");
    }

private:
    void on_wifi_connected() {
        if (mqtt_.started()) {  // WiFi came back: kick the existing client
            const esp_err_t err = mqtt_.reconnect();
            // Not a problem: esp-mqtt also retries by itself and may be doing so already.
            if (err != ESP_OK) ESP_LOGD(TAG, "MQTT reconnect: %s", esp_err_to_name(err));
            return;
        }

        const MQTTClient::Config cfg{.uri = CONFIG_BULB_MQTT_URI,
                                     .client_id = id_.data(),
                                     .username = id_.data(),
                                     .password = CONFIG_BULB_MQTT_PASSWORD,
                                     .lwt_topic = t_status_.data(),
                                     .lwt_message = "offline"};
        ESP_ERROR_CHECK_WITHOUT_ABORT(mqtt_.start(
            cfg, [this] { on_mqtt_connected(); },
            [this](std::string_view topic, std::string_view payload) { on_mqtt_message(topic, payload); }));
    }

    void on_mqtt_connected() {
        mqtt_.publish(t_status_.data(), "online", Qos::AtLeastOnce, true);
        publish_state(ctrl_.state_json().view());
        publish_motion(pir_.motion());
        ESP_ERROR_CHECK_WITHOUT_ABORT(mqtt_.subscribe(t_set_.data(), Qos::AtLeastOnce));
        ESP_LOGI(TAG, "Lowest free stack: light %zu B, pir %zu B", engine_.stack_free_bytes(),
                 pir_.stack_free_bytes());
    }

    void on_mqtt_message(std::string_view topic, std::string_view payload) {
        if (topic == t_set_.data()) ctrl_.apply_json(payload);
    }

    void publish_state(std::string_view json) {
        mqtt_.publish(t_state_.data(), json, Qos::AtLeastOnce, true);
    }

    void publish_motion(bool motion) {
        mqtt_.publish(t_motion_.data(), motion_json(motion), Qos::AtMostOnce, true);
    }

    const BulbId id_ = make_bulb_id();
    const Topic t_status_ = make_topic(id_, "status");
    const Topic t_state_ = make_topic(id_, "state");
    const Topic t_set_ = make_topic(id_, "set");
    const Topic t_motion_ = make_topic(id_, "motion");

    LightEngine engine_;
    BulbCtrl ctrl_{engine_};
    Pir pir_;
    WiFiStation wifi_;
    MQTTClient mqtt_;
};

void init_nvs() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}
}  // namespace

extern "C" void app_main() {
    init_nvs();

    // Lives for the whole run, past the return from app_main. Built here rather
    // than at namespace scope so it's constructed after the scheduler starts.
    static App app;
    app.run();
}
