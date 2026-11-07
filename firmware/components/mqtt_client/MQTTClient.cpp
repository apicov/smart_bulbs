#include "MQTTClient.hpp"
#include <cstddef>
#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"

esp_err_t MQTTClient::start(const Config& cfg, ConnectedCallback on_connected, MessageCallback on_message) {
    ESP_LOGI(TAG, "Initializing with %s as %s", cfg.uri, cfg.client_id);
    on_connected_ = on_connected;
    on_message_ = on_message;

    esp_mqtt_client_config_t mqtt_cfg = {};
    mqtt_cfg.broker.address.uri = cfg.uri;
    // Verify the broker certificate against the IDF CA bundle (covers Let's Encrypt).
    // Ignored for plain mqtt:// URIs.
    mqtt_cfg.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
    mqtt_cfg.session.keepalive = cfg.keepalive_s;
    mqtt_cfg.credentials.client_id = cfg.client_id;
    if (cfg.username) {
        mqtt_cfg.credentials.username = cfg.username;
        mqtt_cfg.credentials.authentication.password = cfg.password;
    }
    if (cfg.lwt_topic) {
        mqtt_cfg.session.last_will.topic = cfg.lwt_topic;
        mqtt_cfg.session.last_will.msg = cfg.lwt_message;
        mqtt_cfg.session.last_will.qos = static_cast<int>(Qos::AtLeastOnce);
        mqtt_cfg.session.last_will.retain = cfg.lwt_retain;
    }

    // Set before the client starts: its first event may arrive at any moment after that.
    client_ = esp_mqtt_client_init(&mqtt_cfg);
    ESP_RETURN_ON_FALSE(client_, ESP_FAIL, TAG, "Failed to initialize MQTT client");

    const auto on_event = [](void* self, esp_event_base_t, int32_t id, void* event) {
        static_cast<MQTTClient*>(self)->handle(id, *static_cast<esp_mqtt_event_handle_t>(event));
    };
    esp_err_t err = ESP_OK;
    for (const esp_mqtt_event_id_t e :
         {MQTT_EVENT_CONNECTED, MQTT_EVENT_DISCONNECTED, MQTT_EVENT_DATA, MQTT_EVENT_ERROR}) {
        if (err == ESP_OK) err = esp_mqtt_client_register_event(client_, e, on_event, this);
    }
    if (err == ESP_OK) err = esp_mqtt_client_start(client_);
    if (err != ESP_OK) {
        esp_mqtt_client_destroy(client_);  // the next WiFi connect tries again
        client_ = nullptr;
    }
    return err;
}

void MQTTClient::handle(int32_t id, const esp_mqtt_event_t& event) {
    switch (id) {
    case MQTT_EVENT_CONNECTED:
        is_connected_.store(true);
        ESP_LOGI(TAG, "Connected to broker");
        if (on_connected_) on_connected_();
        break;
    case MQTT_EVENT_DISCONNECTED:
        is_connected_.store(false);
        ESP_LOGW(TAG, "Disconnected from broker");
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGD(TAG, "Incoming data on %.*s", event.topic_len, event.topic);
        if (on_message_)
            on_message_({event.topic, static_cast<std::size_t>(event.topic_len)},
                        {event.data, static_cast<std::size_t>(event.data_len)}, event.retain);
        break;
    case MQTT_EVENT_ERROR:
        if (event.error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED)
            ESP_LOGE(TAG, "Connection refused (check username/password/ACL), code=%d",
                     event.error_handle->connect_return_code);
        else
            ESP_LOGE(TAG, "MQTT error type=%d", event.error_handle->error_type);
        break;
    default:
        break;
    }
}

int MQTTClient::publish(const char* topic, std::string_view data, Qos qos, bool retain) {
    if (!is_connected()) {
        ESP_LOGD(TAG, "Not connected, dropping publish to %s", topic);
        return -1;
    }
    // enqueue (non-blocking) so callers in other tasks never stall on the network
    const int msg_id = esp_mqtt_client_enqueue(client_, topic, data.data(), static_cast<int>(data.size()),
                                               static_cast<int>(qos), retain, true);
    if (msg_id < 0) ESP_LOGE(TAG, "Failed to publish to %s", topic);
    return msg_id;
}

bool MQTTClient::send_now(const char* topic, std::string_view data) {
    if (!is_connected()) return false;
    return esp_mqtt_client_publish(client_, topic, data.data(), static_cast<int>(data.size()),
                                   static_cast<int>(Qos::AtMostOnce), 0) >= 0;
}

esp_err_t MQTTClient::subscribe(const char* topic, Qos qos) {
    return esp_mqtt_client_subscribe(client_, topic, static_cast<int>(qos)) < 0 ? ESP_FAIL : ESP_OK;
}

esp_err_t MQTTClient::reconnect() { return esp_mqtt_client_reconnect(client_); }
