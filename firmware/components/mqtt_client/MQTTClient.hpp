#pragma once
#include <atomic>
#include <string_view>
#include "Callback.hpp"
#include "esp_err.h"
#include "esp_event.h"
#include "mqtt_client.h"

enum class Qos : int { AtMostOnce = 0, AtLeastOnce = 1 };

/**
 * @brief Thin wrapper around esp-mqtt.
 *
 * Derived from esp32cam_snap's MQTTClient, extended with credentials,
 * client id, Last Will and TLS (mqtts:// / wss://) via the IDF CA bundle.
 *
 * Both callbacks run in the esp-mqtt task. The views passed to them are only
 * valid during the call.
 */
class MQTTClient {
public:
    using ConnectedCallback = Callback<void()>;
    using MessageCallback = Callback<void(std::string_view topic, std::string_view payload, bool retained)>;

    /** esp-mqtt copies these strings, so they only need to outlive start(). */
    struct Config {
        const char* uri;                 ///< e.g. "mqtts://broker.example.com:8883"
        const char* client_id;           ///< unique per device
        const char* username = nullptr;  ///< nullptr = no auth
        const char* password = nullptr;
        const char* lwt_topic = nullptr;  ///< nullptr = no Last Will
        const char* lwt_message = nullptr;
        bool lwt_retain = true;
        int keepalive_s = 15;
    };

    MQTTClient() = default;
    MQTTClient(const MQTTClient&) = delete;
    MQTTClient& operator=(const MQTTClient&) = delete;

    /** Create the client and start connecting. The callbacks are in place before any event fires. */
    [[nodiscard]] esp_err_t start(const Config& cfg, ConnectedCallback on_connected,
                                  MessageCallback on_message);

    /** True once start() has succeeded. Call from the same task as start(). */
    bool started() const { return client_ != nullptr; }

    bool is_connected() const { return is_connected_.load(); }

    /** Queue a payload for sending, from any task. Returns msg id, or -1 on failure. */
    int publish(const char* topic, std::string_view data, Qos qos = Qos::AtMostOnce, bool retain = false);

    /**
     * Send straight away, unretained and at QoS 0, for high-volume data. Blocks until it's
     * written, unlike publish(), whose queue only moves about one message per poll.
     */
    bool send_now(const char* topic, std::string_view data);

    [[nodiscard]] esp_err_t subscribe(const char* topic, Qos qos = Qos::AtMostOnce);

    [[nodiscard]] esp_err_t reconnect();

private:
    static constexpr const char* TAG = "mqtt_client";

    std::atomic<bool> is_connected_{false};
    esp_mqtt_client_handle_t client_ = nullptr;
    ConnectedCallback on_connected_;
    MessageCallback on_message_;

    void handle(int32_t id, const esp_mqtt_event_t& event);
};
