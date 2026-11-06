#pragma once
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <type_traits>
#include "LightModel.hpp"

// The bulb's data formats, free of ESP-IDF so they can be unit tested on the host.
//
// Wire format follows Home Assistant's MQTT "json" light schema:
//   set:   {"state":"ON"|"OFF"|"TOGGLE", "brightness":0-255,
//           "color":{"r":..,"g":..,"b":..} (or [r,g,b]), "transition":seconds}
//   state: {"state":"ON","brightness":128,"color_mode":"rgb","color":{"r":..,"g":..,"b":..}}
// All set fields are optional.

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
