#pragma once
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <type_traits>
#include "BulbSchedule.hpp"
#include "LightModel.hpp"

// The bulb's data formats, free of ESP-IDF so they can be unit tested on the host.
//
// Wire format follows Home Assistant's MQTT "json" light schema:
//   set:   {"state":"ON"|"OFF"|"TOGGLE", "brightness":0-255,
//           "color":{"r":..,"g":..,"b":..} (or [r,g,b]), "transition":seconds}
//   state: {"state":"ON","brightness":128,"color_mode":"rgb","color":{"r":..,"g":..,"b":..}}
// All set fields are optional.
//
// Sleep timer and alarm, on their own topics:
//   schedule_set: {"timer":{"after":seconds,"fade":seconds}} or {"timer":null},
//                 {"alarm":{"enabled":true,"hour":7,"minute":0,"days":[1,2,3,4,5],"rise":seconds,
//                           "brightness":1-255,"color":{"r":..,"g":..,"b":..}}}
//                 All fields but the timer's "after" are optional. Days are 0 (Sunday) to 6, and
//                 an empty list means once.
//   schedule:     {"time_synced":true,"timer":{"ends":unix_time,"fade":seconds},"alarm":{...}}
//                 "timer" is null when none is running, and "ends" is 0 until the clock is set.
//
// Maintenance, on their own topics:
//   cmd:  {"cmd":"ota"}, or {"cmd":"ota","force":true} to retry a version that was rolled back
//   info: {"version":"abc1234","confirmed":true,"rolled_back":null,"ota":"idle","progress":0}

struct Command {
    LightState state;
    std::chrono::milliseconds transition;
};

/** Apply a `set` payload to `current`. Empty if it is not a JSON object. */
[[nodiscard]] std::optional<Command> parse_command(std::string_view json, const LightState& current);

/** A formatted state message, big enough for the longest possible one. */
class StateJson {
public:
    [[nodiscard]] std::string_view view() const { return {buf_.data(), len_}; }

private:
    friend StateJson format_state(const LightState& s);
    std::array<char, 96> buf_{};
    std::size_t len_ = 0;
};

[[nodiscard]] StateJson format_state(const LightState& s);

struct ScheduleCommand {
    enum class TimerAction : uint8_t { None, Start, Cancel };
    TimerAction timer_action = TimerAction::None;
    SleepTimer timer{};
    std::optional<Alarm> alarm;  ///< Set if the message changes the alarm.
};

/** Parse a `schedule_set` payload, applying its alarm fields to `current`. Empty if not a JSON object. */
[[nodiscard]] std::optional<ScheduleCommand> parse_schedule_command(std::string_view json,
                                                                    const Alarm& current);

/** A running sleep timer, as reported on `schedule`. */
struct TimerStatus {
    int64_t ends;  ///< Unix time at which the light is fully off, 0 if the clock isn't set.
    uint32_t fade_s;
};

/** A formatted schedule message, big enough for the longest possible one. */
class ScheduleJson {
public:
    [[nodiscard]] std::string_view view() const { return {buf_.data(), len_}; }

private:
    friend ScheduleJson format_schedule(bool time_synced, const std::optional<TimerStatus>& timer,
                                        const Alarm& alarm);
    std::array<char, 256> buf_{};
    std::size_t len_ = 0;
};

[[nodiscard]] ScheduleJson format_schedule(bool time_synced, const std::optional<TimerStatus>& timer,
                                           const Alarm& alarm);

struct DebugCommand {
    enum class Kind : uint8_t { Ota };
    Kind kind;
    bool force = false;
};

/** Parse a `cmd` payload. Empty if it isn't a command this firmware knows. */
[[nodiscard]] std::optional<DebugCommand> parse_debug_command(std::string_view json);

/** What's reported on `info`. */
struct DeviceInfo {
    std::string_view version;
    bool confirmed;                ///< False until a new image has proved itself.
    std::string_view rolled_back;  ///< The version that failed here, or empty.
    const char* ota;               ///< One of ota_status_name()'s fixed names.
    uint8_t progress;
};

/** A formatted info message, big enough for the longest possible one. */
class InfoJson {
public:
    [[nodiscard]] std::string_view view() const { return {buf_.data(), len_}; }

private:
    friend InfoJson format_info(const DeviceInfo& info);
    std::array<char, 192> buf_{};
    std::size_t len_ = 0;
};

[[nodiscard]] InfoJson format_info(const DeviceInfo& info);

/**
 * Versioned NVS record, so the struct can grow later without misreading old
 * data. Bulbs in the field hold this exact layout: changing it needs a new
 * version.
 */
struct StoredState {
    static constexpr uint8_t VERSION = 1;
    uint8_t version = VERSION;
    LightState state;
};
static_assert(sizeof(StoredState) == 6);
static_assert(std::is_trivially_copyable_v<StoredState>);

/** Versioned NVS record for the alarm, kept apart from StoredState. */
struct StoredAlarm {
    static constexpr uint8_t VERSION = 1;
    uint8_t version = VERSION;
    Alarm alarm;
};
static_assert(sizeof(StoredAlarm) == 12);
static_assert(std::is_trivially_copyable_v<StoredAlarm>);
