#pragma once
#include <atomic>
#include <functional>
#include <string>
#include <vector>
#include "mqtt_client.h"
#include "esp_event.h"
#include "esp_log.h"

/**
 * @brief Thin wrapper around esp-mqtt.
 *
 * Derived from esp32cam_snap's MQTTClient, extended with credentials,
 * client id, Last Will and TLS (mqtts:// / wss://) via the IDF CA bundle.
 */
class MQTTClient {
public:
    using MQTTEventCallback = std::function<void(esp_mqtt_event_handle_t)>;

    struct Config {
        std::string uri;          ///< e.g. "mqtts://broker.example.com:8883"
        std::string client_id;    ///< unique per device
        std::string username;     ///< empty = no auth
        std::string password;
        std::string lwt_topic;    ///< empty = no Last Will
        std::string lwt_message;
        bool lwt_retain = true;
        int keepalive_s = 15;
    };

    explicit MQTTClient(Config cfg);

    bool is_connected() const;

    /** Publish a NUL-terminated payload. Returns msg id, or -1 on failure. */
    int publish(const char* topic, const char* data, int qos = 0, bool retain = false);

    esp_err_t subscribe(const char* topic, int qos = 0);

    esp_err_t reconnect();

    MQTTClient& on_connect(MQTTEventCallback);
    MQTTClient& on_disconnect(MQTTEventCallback);
    MQTTClient& on_data_received(MQTTEventCallback);

private:
    static constexpr const char* TAG = "mqtt_client";

    Config cfg_;  ///< owns the strings referenced by the esp-mqtt config
    std::atomic<bool> is_connected_{false};
    esp_mqtt_client_handle_t mqtt_client_ = nullptr;
    std::vector<MQTTEventCallback> on_connect_cb;
    std::vector<MQTTEventCallback> on_disconnect_cb;
    std::vector<MQTTEventCallback> on_data_received_cb;

    static void event_handler(void*, esp_event_base_t, int32_t, void*);
    void handle(esp_event_base_t, int32_t, esp_mqtt_event_handle_t);
};
