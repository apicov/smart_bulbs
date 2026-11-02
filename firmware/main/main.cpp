#include <cstring>
#include <string>

#include <esp_log.h>
#include <esp_mac.h>
#include <nvs_flash.h>

#include <BulbCtrl.hpp>
#include <LightEngine.hpp>
#include <MQTTClient.hpp>
#include <Pir.hpp>
#include <WiFiStation.hpp>

#define CONF(name) CONFIG_BULB_##name

namespace {
const char* TAG = "main";

std::string bulb_id;
std::string t_status, t_state, t_set, t_motion;

LightEngine engine;
BulbCtrl ctrl;
Pir pir;
MQTTClient* mqtt = nullptr;

std::string make_bulb_id() {
    if (strlen(CONF(ID)) > 0) return CONF(ID);
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char buf[16];
    snprintf(buf, sizeof buf, "bulb-%02x%02x%02x", mac[3], mac[4], mac[5]);
    return buf;
}

void publish_state(const std::string& json) {
    if (mqtt) mqtt->publish(t_state.c_str(), json.c_str(), 1, true);
}

void start_mqtt_client() {
    if (mqtt) {  // WiFi came back: kick the existing client
        mqtt->reconnect();
        return;
    }

    MQTTClient::Config cfg;
    cfg.uri = CONF(MQTT_URI);
    cfg.client_id = bulb_id;
    cfg.username = bulb_id;
    cfg.password = CONF(MQTT_PASSWORD);
    cfg.lwt_topic = t_status;
    cfg.lwt_message = "offline";

    mqtt = new MQTTClient{cfg};
    mqtt->on_connect([](auto) {
        mqtt->publish(t_status.c_str(), "online", 1, true);
        publish_state(ctrl.state_json());
        mqtt->publish(t_motion.c_str(), pir.motion() ? R"({"motion":true})" : R"({"motion":false})", 0, true);
        mqtt->subscribe(t_set.c_str(), 1);
    });
    mqtt->on_data_received([](esp_mqtt_event_handle_t ev) {
        if (ev->topic_len == (int)t_set.size() && !strncmp(ev->topic, t_set.data(), ev->topic_len))
            ctrl.apply_json(ev->data, ev->data_len);
    });
}
}  // namespace

extern "C" void app_main() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    bulb_id = make_bulb_id();
    const std::string base = std::string(CONF(TOPIC_PREFIX)) + "/" + bulb_id + "/";
    t_status = base + "status";
    t_state = base + "state";
    t_set = base + "set";
    t_motion = base + "motion";
    ESP_LOGI(TAG, "Bulb id: %s (topics %s*)", bulb_id.c_str(), base.c_str());

    // Light first, so the bulb works as a plain lamp even without WiFi.
    ESP_ERROR_CHECK(engine.start({CONF(LED_GPIO), CONF(LED_COUNT), CONF(MAX_MILLIAMPS)}));
#ifdef CONFIG_BULB_POWER_ON_LIGHT
    ctrl.begin(engine, true, publish_state);
#else
    ctrl.begin(engine, false, publish_state);
#endif

    if (CONF(PIR_GPIO) >= 0) {
        pir.start(static_cast<gpio_num_t>(CONF(PIR_GPIO)), [](bool motion) {
            if (mqtt)
                mqtt->publish(t_motion.c_str(), motion ? R"({"motion":true})" : R"({"motion":false})", 0, true);
        });
    }

    WiFiStation::start(CONF(WIFI_SSID), CONF(WIFI_PASSWORD))
        .on_connect([](auto) { start_mqtt_client(); });

    ESP_LOGI(TAG, "pico-bulb is running");
}
