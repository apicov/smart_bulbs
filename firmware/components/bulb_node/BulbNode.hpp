#pragma once
#include <array>
#include <cstddef>
#include <optional>
#include <string_view>
#include <tuple>

#include "sdkconfig.h"

#include <BulbConfig.hpp>
#include <BulbCtrl.hpp>
#include <LightEngine.hpp>
#include <MQTTClient.hpp>
#include <OtaUpdater.hpp>
#include <Pir.hpp>
#include <RemoteDebug.hpp>
#include <WiFiStation.hpp>

/**
 * A light bulb on MQTT: owns every part of it and wires them together.
 *
 * Each chip's firmware builds one of these in app_main. Firmware that does more
 * than light up (sensors, a camera) publishes its extra topics with publish().
 */
class BulbNode {
public:
    /** Mount NVS, wiping it if the layout changed. Call before constructing a BulbNode. */
    static void init_nvs();

    BulbNode();
    BulbNode(const BulbNode&) = delete;
    BulbNode& operator=(const BulbNode&) = delete;

    /** Start the light, then everything else. Returns once all tasks are running. */
    void run();

    [[nodiscard]] bool connected() const { return mqtt_.is_connected(); }

    /**
     * Publish on this bulb's own topic `<prefix>/<id>/<leaf>`. Thread-safe.
     * Dropped while offline, so retain anything a late subscriber needs.
     */
    void publish(std::string_view leaf, std::string_view payload, bool retain);

private:
    // sizeof includes each string's NUL, which leaves room for the separators too,
    // so topics built from these can't be truncated.
    static constexpr std::size_t ID_CAPACITY = std::tuple_size_v<decltype(BulbConfig::id)>;
    static constexpr std::string_view LONGEST_LEAF = "schedule_set";
    static constexpr std::size_t TOPIC_CAPACITY =
        sizeof(CONFIG_BULB_TOPIC_PREFIX) + ID_CAPACITY + LONGEST_LEAF.size() + 1;

    using BulbId = std::array<char, ID_CAPACITY>;
    using Topic = std::array<char, TOPIC_CAPACITY>;

    static BulbId make_bulb_id(const std::optional<BulbConfig>& config);
    static Topic make_topic(const BulbId& id, std::string_view leaf);

    void on_wifi_connected();
    void start_time_sync();
    void on_mqtt_connected();
    void on_mqtt_message(std::string_view topic, std::string_view payload, bool retained);
    void on_command(std::string_view payload);
    void publish_info();
    void publish_state(std::string_view json);
    void publish_schedule(std::string_view json);
    void publish_motion(bool motion);

    const std::optional<BulbConfig> config_;
    const BulbId id_;
    const Topic t_status_, t_state_, t_set_, t_motion_, t_schedule_, t_schedule_set_, t_cmd_, t_info_, t_log_,
        t_coredump_;

    LightEngine engine_;
    BulbCtrl ctrl_{engine_};
    Pir pir_;
    WiFiStation wifi_;
    MQTTClient mqtt_;
    OtaUpdater ota_;
    RemoteDebug debug_;
    bool time_sync_started_ = false;
};
