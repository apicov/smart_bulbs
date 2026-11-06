#include "WiFiStation.hpp"
#include <algorithm>
#include <cstring>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_netif.h"

esp_err_t WiFiStation::start(const char* ssid, const char* password, ConnectedCallback on_connected) {
    on_connected_ = on_connected;

    esp_timer_create_args_t timer_args = {};
    timer_args.callback = [](void* self) { static_cast<WiFiStation*>(self)->connect(); };
    timer_args.arg = this;
    timer_args.name = "wifi_retry";
    ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &retry_timer_), TAG, "retry timer");

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif_init");

    const esp_err_t loop_err = esp_event_loop_create_default();
    if (loop_err != ESP_ERR_INVALID_STATE)  // already created is fine
        ESP_RETURN_ON_ERROR(loop_err, TAG, "event loop");

    ESP_RETURN_ON_FALSE(esp_netif_create_default_wifi_sta(), ESP_FAIL, TAG, "netif");

    const wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_cfg), TAG, "esp_wifi_init");

    const auto on_event = [](void* self, esp_event_base_t base, int32_t id, void* data) {
        static_cast<WiFiStation*>(self)->handle(base, id, data);
    };
    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_START, on_event, this, nullptr), TAG,
        "register STA_START");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, on_event, this, nullptr),
        TAG, "register STA_DISCONNECTED");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, this, nullptr), TAG,
        "register GOT_IP");

    wifi_config_t config = {};
    strlcpy(reinterpret_cast<char*>(config.sta.ssid), ssid, sizeof config.sta.ssid);
    strlcpy(reinterpret_cast<char*>(config.sta.password), password, sizeof config.sta.password);
    config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &config), TAG, "set config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "esp_wifi_start");

    // Mains powered: disable modem sleep so MQTT commands arrive without latency
    ESP_RETURN_ON_ERROR(esp_wifi_set_ps(WIFI_PS_NONE), TAG, "set power save");

    ESP_LOGI(TAG, "Wi-Fi initialized in Station mode");
    return ESP_OK;
}

void WiFiStation::handle(esp_event_base_t base, int32_t id, void* data) {
    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_STA_START) {
            ESP_LOGI(TAG, "started, connecting...");
            connect();
        } else {
            schedule_retry();
        }
    } else if (base == IP_EVENT) {
        retry_delay_s_.store(MIN_RETRY_DELAY_S);
        const auto* event = static_cast<const ip_event_got_ip_t*>(data);
        ESP_LOGI(TAG, "Got IP " IPSTR, IP2STR(&event->ip_info.ip));
        if (on_connected_) on_connected_();
    }
}

void WiFiStation::connect() {
    const esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        // A failed call produces no disconnect event, so nothing else would retry.
        ESP_LOGW(TAG, "esp_wifi_connect: %s", esp_err_to_name(err));
        schedule_retry();
    }
}

void WiFiStation::schedule_retry() {
    if (esp_timer_is_active(retry_timer_)) return;

    const uint32_t delay_s = retry_delay_s_.load();
    ESP_LOGI(TAG, "disconnected, retrying in %lu s", static_cast<unsigned long>(delay_s));
    const esp_err_t err = esp_timer_start_once(retry_timer_, uint64_t{delay_s} * 1000 * 1000);
    if (err != ESP_OK) ESP_LOGE(TAG, "retry timer: %s", esp_err_to_name(err));
    retry_delay_s_.store(std::min(delay_s * 2, MAX_RETRY_DELAY_S));
}
