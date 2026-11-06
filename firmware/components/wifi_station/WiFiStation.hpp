#pragma once
#include <atomic>
#include <cstdint>
#include "Callback.hpp"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "esp_wifi.h"

/**
 * @brief A thin wrapper around the ESP-IDF "esp_wifi" for quick use.
 *
 * Connects in station mode and keeps retrying whenever the connection drops,
 * waiting longer between attempts while the network stays unreachable.
 * Start it only once: it sets up the process-wide network stack.
 */
class WiFiStation {
public:
    using ConnectedCallback = Callback<void()>;

    WiFiStation() = default;
    WiFiStation(const WiFiStation&) = delete;
    WiFiStation& operator=(const WiFiStation&) = delete;

    /**
     * @brief Start connecting to the given network.
     *
     * @param on_connected Called from the event task each time an IP is obtained.
     */
    [[nodiscard]] esp_err_t start(const char* ssid, const char* password, ConnectedCallback on_connected);

private:
    static constexpr const char* TAG = "wifi_station";
    static constexpr uint32_t MIN_RETRY_DELAY_S = 1;
    static constexpr uint32_t MAX_RETRY_DELAY_S = 30;

    ConnectedCallback on_connected_;
    esp_timer_handle_t retry_timer_ = nullptr;
    std::atomic<uint32_t> retry_delay_s_{MIN_RETRY_DELAY_S};  // used by the event and timer tasks

    void handle(esp_event_base_t base, int32_t id, void* data);
    void connect();
    void schedule_retry();
};
