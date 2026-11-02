#include "MQTTClient.hpp"
#include "esp_crt_bundle.h"

MQTTClient::MQTTClient(Config cfg) : cfg_(std::move(cfg)) {
    ESP_LOGI(TAG, "Initializing with %s as %s", cfg_.uri.c_str(), cfg_.client_id.c_str());

    esp_mqtt_client_config_t mqtt_cfg = {};
    mqtt_cfg.broker.address.uri = cfg_.uri.c_str();
    // Verify the broker certificate against the IDF CA bundle (covers Let's Encrypt).
    // Ignored for plain mqtt:// URIs.
    mqtt_cfg.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
    mqtt_cfg.session.keepalive = cfg_.keepalive_s;

    if (!cfg_.client_id.empty()) mqtt_cfg.credentials.client_id = cfg_.client_id.c_str();
    if (!cfg_.username.empty()) {
        mqtt_cfg.credentials.username = cfg_.username.c_str();
        mqtt_cfg.credentials.authentication.password = cfg_.password.c_str();
    }
    if (!cfg_.lwt_topic.empty()) {
        mqtt_cfg.session.last_will.topic = cfg_.lwt_topic.c_str();
        mqtt_cfg.session.last_will.msg = cfg_.lwt_message.c_str();
        mqtt_cfg.session.last_will.qos = 1;
        mqtt_cfg.session.last_will.retain = cfg_.lwt_retain;
    }

    if (!(mqtt_client_ = esp_mqtt_client_init(&mqtt_cfg))) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return;
    }

    for (const esp_mqtt_event_id_t& e : {MQTT_EVENT_CONNECTED, MQTT_EVENT_DISCONNECTED,
                                         MQTT_EVENT_DATA, MQTT_EVENT_ERROR})
        esp_mqtt_client_register_event(mqtt_client_, e, event_handler, this);

    esp_mqtt_client_start(mqtt_client_);
}

void MQTTClient::event_handler(void* arg, esp_event_base_t base, int32_t id, void* data) {
    static_cast<MQTTClient*>(arg)->handle(base, id, (esp_mqtt_event_handle_t)data);
}

void MQTTClient::handle(esp_event_base_t, int32_t id, esp_mqtt_event_handle_t data) {
    switch (id) {
    case MQTT_EVENT_CONNECTED:
        is_connected_.store(true);
        ESP_LOGI(TAG, "Connected to broker");
        for (const auto& f : on_connect_cb) f(data);
        break;
    case MQTT_EVENT_DISCONNECTED:
        is_connected_.store(false);
        ESP_LOGW(TAG, "Disconnected from broker");
        for (const auto& f : on_disconnect_cb) f(data);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGD(TAG, "Incoming data on %.*s", data->topic_len, data->topic);
        for (const auto& f : on_data_received_cb) f(data);
        break;
    case MQTT_EVENT_ERROR:
        if (data->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED)
            ESP_LOGE(TAG, "Connection refused (check username/password/ACL), code=%d",
                     data->error_handle->connect_return_code);
        else
            ESP_LOGE(TAG, "MQTT error type=%d", data->error_handle->error_type);
        break;
    }
}

MQTTClient& MQTTClient::on_connect(MQTTEventCallback f) {
    on_connect_cb.push_back(std::move(f));
    return *this;
}
MQTTClient& MQTTClient::on_disconnect(MQTTEventCallback f) {
    on_disconnect_cb.push_back(std::move(f));
    return *this;
}
MQTTClient& MQTTClient::on_data_received(MQTTEventCallback f) {
    on_data_received_cb.push_back(std::move(f));
    return *this;
}

bool MQTTClient::is_connected() const { return is_connected_.load(); }

int MQTTClient::publish(const char* topic, const char* data, int qos, bool retain) {
    if (!is_connected()) {
        ESP_LOGD(TAG, "Not connected, dropping publish to %s", topic);
        return -1;
    }
    // enqueue (non-blocking) so callers in other tasks never stall on the network
    int msg_id = esp_mqtt_client_enqueue(mqtt_client_, topic, data, 0, qos, retain, true);
    if (msg_id < 0) ESP_LOGE(TAG, "Failed to publish to %s", topic);
    return msg_id;
}

esp_err_t MQTTClient::subscribe(const char* topic, int qos) {
    return esp_mqtt_client_subscribe(mqtt_client_, topic, qos) < 0 ? ESP_FAIL : ESP_OK;
}

esp_err_t MQTTClient::reconnect() { return esp_mqtt_client_reconnect(mqtt_client_); }
