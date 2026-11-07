#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <optional>
#include <string_view>
#include <tuple>

#include <esp_log.h>
#include <esp_mac.h>
#include <esp_netif_sntp.h>
#include <esp_system.h>
#include <nvs_flash.h>

#include <BulbConfig.hpp>
#include <BulbCtrl.hpp>
#include <LightEngine.hpp>
#include <MQTTClient.hpp>
#include <OtaUpdater.hpp>
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
static_assert(CONFIG_BULB_MAX_MILLIAMPS >= 0, "current limit can't be negative");

// sizeof includes each string's NUL, which leaves room for the separators too,
// so topics built from these can't be truncated.
constexpr std::size_t ID_CAPACITY = std::tuple_size_v<decltype(BulbConfig::id)>;
// "schedule_set" is the longest leaf.
constexpr std::size_t TOPIC_CAPACITY =
    sizeof(CONFIG_BULB_TOPIC_PREFIX) + ID_CAPACITY + sizeof("schedule_set");

using BulbId = std::array<char, ID_CAPACITY>;
using Topic = std::array<char, TOPIC_CAPACITY>;

/** The provisioned id. An unprovisioned bulb stays offline, so this is only for its logs. */
BulbId make_bulb_id(const std::optional<BulbConfig>& config) {
    if (config) return config->id;
    BulbId id{};
    std::array<uint8_t, 6> mac{};
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_read_mac(mac.data(), ESP_MAC_WIFI_STA));
    (void)std::snprintf(id.data(), id.size(), "bulb-%02x%02x%02x", mac[3], mac[4], mac[5]);
    return id;
}

/** True if the firmware restarted itself. A wall switch or power cut causes any other reset. */
bool restarted_by_firmware() {
    switch (esp_reset_reason()) {
    case ESP_RST_SW:
    case ESP_RST_PANIC:
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
        return true;
    default:
        return false;
    }
}

Topic make_topic(const BulbId& id, const char* leaf) {
    Topic topic{};
    (void)std::snprintf(topic.data(), topic.size(), "%s/%s/%s", CONFIG_BULB_TOPIC_PREFIX, id.data(), leaf);
    return topic;
}

constexpr const char* NTP_SERVER = "pool.ntp.org";

constexpr std::string_view motion_json(bool motion) {
    return motion ? R"({"motion":true})" : R"({"motion":false})";
}

/** Owns every part of the bulb and wires them together. */
class App {
public:
    void run() {
        ESP_LOGI(TAG, "Bulb id: %s (topics %s/%s/*)", id_.data(), CONFIG_BULB_TOPIC_PREFIX, id_.data());

        // Local time, once SNTP has set the clock. Daylight saving follows from the rule.
        setenv("TZ", CONFIG_BULB_TIMEZONE, 1);  // NOLINT(concurrency-mt-unsafe): no other task uses it yet
        tzset();

        // Light first, so the bulb works as a plain lamp even without WiFi.
        ESP_ERROR_CHECK(engine_.start(
            {.gpio = CONFIG_BULB_LED_GPIO,
             .led_count = config_ ? config_->led_count : uint16_t{CONFIG_BULB_LED_COUNT},
             .max_milliamps = config_ ? config_->max_milliamps : uint32_t{CONFIG_BULB_MAX_MILLIAMPS}}));
        // An update or crash shouldn't turn on a light that was off.
        ESP_ERROR_CHECK(ctrl_.start(
            POWER_ON_LIGHT && !restarted_by_firmware(),
            [this](std::string_view json) { publish_state(json); },
            [this](std::string_view json) { publish_schedule(json); }));

        // Past this point a failure costs a feature, not the lamp: log it and carry on.
        if constexpr (CONFIG_BULB_PIR_GPIO >= 0) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(pir_.start(static_cast<gpio_num_t>(CONFIG_BULB_PIR_GPIO),
                                                     [this](bool motion) { publish_motion(motion); }));
        }
        if (config_) {
            ota_.start(CONFIG_BULB_OTA_URL, {.busy = [this] { return ctrl_.schedule_active(); },
                                             .connected = [this] { return mqtt_.is_connected(); },
                                             .before_restart = [this] { ctrl_.flush(); },
                                             .on_status = [this] { publish_info(); }});
            ESP_ERROR_CHECK_WITHOUT_ABORT(wifi_.start(
                config_->wifi_ssid.data(), config_->wifi_password.data(), [this] { on_wifi_connected(); }));
        } else {
            ESP_LOGW(TAG, "No WiFi or MQTT settings: running as a plain lamp");
        }

        ESP_LOGI(TAG, "pico-bulb is running");
    }

private:
    void on_wifi_connected() {
        if (!config_) return;  // WiFi isn't started without settings
        start_time_sync();

        if (mqtt_.started()) {  // WiFi came back: kick the existing client
            const esp_err_t err = mqtt_.reconnect();
            // Not a problem: esp-mqtt also retries by itself and may be doing so already.
            if (err != ESP_OK) ESP_LOGD(TAG, "MQTT reconnect: %s", esp_err_to_name(err));
            return;
        }

        const MQTTClient::Config cfg{.uri = config_->mqtt_uri.data(),
                                     .client_id = id_.data(),
                                     .username = id_.data(),
                                     .password = config_->mqtt_password.data(),
                                     .lwt_topic = t_status_.data(),
                                     .lwt_message = "offline"};
        ESP_ERROR_CHECK_WITHOUT_ABORT(mqtt_.start(
            cfg, [this] { on_mqtt_connected(); },
            [this](std::string_view topic, std::string_view payload, bool retained) {
                on_mqtt_message(topic, payload, retained);
            }));
    }

    void start_time_sync() {
        if (time_sync_started_) return;
        esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(NTP_SERVER);
        cfg.wait_for_sync = false;  // BulbCtrl notices when the clock becomes valid
        const esp_err_t err = esp_netif_sntp_init(&cfg);
        ESP_ERROR_CHECK_WITHOUT_ABORT(err);
        time_sync_started_ = err == ESP_OK;
    }

    void on_mqtt_connected() {
        mqtt_.publish(t_status_.data(), "online", Qos::AtLeastOnce, true);
        publish_state(ctrl_.state_json().view());
        publish_motion(pir_.motion());
        publish_schedule(ctrl_.schedule_json().view());
        publish_info();
        ESP_ERROR_CHECK_WITHOUT_ABORT(mqtt_.subscribe(t_set_.data(), Qos::AtLeastOnce));
        ESP_ERROR_CHECK_WITHOUT_ABORT(mqtt_.subscribe(t_schedule_set_.data(), Qos::AtLeastOnce));
        ESP_ERROR_CHECK_WITHOUT_ABORT(mqtt_.subscribe(t_cmd_.data(), Qos::AtLeastOnce));
        ESP_LOGI(TAG, "Lowest free stack: light %zu B, pir %zu B, schedule %zu B, ota %zu B",
                 engine_.stack_free_bytes(), pir_.stack_free_bytes(), ctrl_.stack_free_bytes(),
                 ota_.stack_free_bytes());
    }

    void on_mqtt_message(std::string_view topic, std::string_view payload, bool retained) {
        if (topic == t_set_.data())
            ctrl_.apply_json(payload);
        else if (topic == t_schedule_set_.data())
            ctrl_.apply_schedule_json(payload);
        else if (topic == t_cmd_.data() && !retained)  // a stale command mustn't repeat on every connect
            on_command(payload);
    }

    void on_command(std::string_view payload) {
        const auto cmd = parse_debug_command(payload);
        if (!cmd) {
            ESP_LOGW(TAG, "Ignoring unknown command: %.*s", static_cast<int>(payload.size()), payload.data());
            return;
        }
        ota_.request(cmd->force);
    }

    void publish_info() {
        const InfoJson json = format_info({.version = OtaUpdater::running_version(),
                                           .confirmed = ota_.confirmed(),
                                           .rolled_back = ota_.rolled_back_version(),
                                           .ota = ota_status_name(ota_.status()),
                                           .progress = ota_.progress()});
        mqtt_.publish(t_info_.data(), json.view(), Qos::AtLeastOnce, true);
    }

    void publish_state(std::string_view json) {
        mqtt_.publish(t_state_.data(), json, Qos::AtLeastOnce, true);
    }

    void publish_schedule(std::string_view json) {
        mqtt_.publish(t_schedule_.data(), json, Qos::AtLeastOnce, true);
    }

    void publish_motion(bool motion) {
        mqtt_.publish(t_motion_.data(), motion_json(motion), Qos::AtMostOnce, true);
    }

    const std::optional<BulbConfig> config_ =
        load_bulb_config(CONFIG_BULB_MAX_MILLIAMPS, CONFIG_BULB_LED_COUNT);
    const BulbId id_ = make_bulb_id(config_);
    const Topic t_status_ = make_topic(id_, "status");
    const Topic t_state_ = make_topic(id_, "state");
    const Topic t_set_ = make_topic(id_, "set");
    const Topic t_motion_ = make_topic(id_, "motion");
    const Topic t_schedule_ = make_topic(id_, "schedule");
    const Topic t_schedule_set_ = make_topic(id_, "schedule_set");
    const Topic t_cmd_ = make_topic(id_, "cmd");
    const Topic t_info_ = make_topic(id_, "info");

    LightEngine engine_;
    BulbCtrl ctrl_{engine_};
    Pir pir_;
    WiFiStation wifi_;
    MQTTClient mqtt_;
    OtaUpdater ota_;
    bool time_sync_started_ = false;
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
