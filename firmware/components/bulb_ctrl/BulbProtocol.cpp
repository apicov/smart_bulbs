#include "BulbProtocol.hpp"
#include <algorithm>
#include <cstdio>
#include <memory>
#include <strings.h>
#include "cJSON.h"

namespace {
constexpr std::chrono::milliseconds DEFAULT_TRANSITION{400};
constexpr double MAX_TRANSITION_S = 3600.0;

using JsonPtr = std::unique_ptr<cJSON, decltype([](cJSON* json) { cJSON_Delete(json); })>;

std::optional<double> as_number(const cJSON* n) {
    if (n == nullptr || !cJSON_IsNumber(n)) return std::nullopt;
    return n->valuedouble;
}

uint8_t to_u8(double value) { return static_cast<uint8_t>(std::clamp(value, 0.0, 255.0)); }

std::optional<Rgb> parse_color(const cJSON* c) {
    std::optional<double> r;
    std::optional<double> g;
    std::optional<double> b;
    if (cJSON_IsArray(c) && cJSON_GetArraySize(c) == 3) {
        r = as_number(cJSON_GetArrayItem(c, 0));
        g = as_number(cJSON_GetArrayItem(c, 1));
        b = as_number(cJSON_GetArrayItem(c, 2));
    } else if (cJSON_IsObject(c)) {
        r = as_number(cJSON_GetObjectItem(c, "r"));
        g = as_number(cJSON_GetObjectItem(c, "g"));
        b = as_number(cJSON_GetObjectItem(c, "b"));
    }
    if (!r || !g || !b) return std::nullopt;
    return Rgb{.r = to_u8(*r), .g = to_u8(*g), .b = to_u8(*b)};
}
}  // namespace

std::optional<Command> parse_command(std::string_view json, const LightState& current) {
    const JsonPtr root{cJSON_ParseWithLength(json.data(), json.size())};
    if (!cJSON_IsObject(root.get())) return std::nullopt;

    Command cmd{.state = current, .transition = DEFAULT_TRANSITION};
    LightState& next = cmd.state;

    const cJSON* st = cJSON_GetObjectItem(root.get(), "state");
    if (cJSON_IsString(st)) {
        if (!strcasecmp(st->valuestring, "ON"))
            next.on = true;
        else if (!strcasecmp(st->valuestring, "OFF"))
            next.on = false;
        else if (!strcasecmp(st->valuestring, "TOGGLE"))
            next.on = !next.on;
    } else if (cJSON_IsBool(st)) {
        next.on = cJSON_IsTrue(st);
    }

    if (const auto brightness = as_number(cJSON_GetObjectItem(root.get(), "brightness"))) {
        const uint8_t b = to_u8(*brightness);
        if (b == 0)
            next.on = false;  // keep the last non-zero brightness for the next "ON"
        else
            next.brightness = b;
    }

    if (const auto color = parse_color(cJSON_GetObjectItem(root.get(), "color"))) next.color = *color;

    if (const auto transition = as_number(cJSON_GetObjectItem(root.get(), "transition"))) {
        const double seconds = std::clamp(*transition, 0.0, MAX_TRANSITION_S);
        cmd.transition = std::chrono::milliseconds{static_cast<int64_t>(seconds * 1000.0)};
    }
    return cmd;
}

StateJson format_state(const LightState& s) {
    StateJson out;
    const int n =
        std::snprintf(out.buf_.data(), out.buf_.size(),
                      R"({"state":"%s","brightness":%u,"color_mode":"rgb","color":{"r":%u,"g":%u,"b":%u}})",
                      s.on ? "ON" : "OFF", s.brightness, s.color.r, s.color.g, s.color.b);
    out.len_ = static_cast<std::size_t>(n);
    return out;
}
