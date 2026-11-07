#include "BulbProtocol.hpp"
#include <algorithm>
#include <cstdio>
#include <memory>
#include <strings.h>
#include "cJSON.h"

namespace {
constexpr std::chrono::milliseconds DEFAULT_TRANSITION{400};
constexpr double MAX_TRANSITION_S = 3600.0;
constexpr double MAX_TIMER_S = SECONDS_PER_DAY;
constexpr double MIN_RISE_S = 5.0;
constexpr double MAX_RISE_S = 3600.0;

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

void parse_timer(const cJSON* timer, ScheduleCommand& cmd) {
    if (cJSON_IsNull(timer)) {
        cmd.timer_action = ScheduleCommand::TimerAction::Cancel;
        return;
    }
    const auto after = as_number(cJSON_GetObjectItem(timer, "after"));
    if (!after) return;
    if (*after < 1.0) {
        cmd.timer_action = ScheduleCommand::TimerAction::Cancel;
        return;
    }
    const double after_s = std::min(*after, MAX_TIMER_S);
    const double fade_s = std::clamp(as_number(cJSON_GetObjectItem(timer, "fade")).value_or(0.0), 0.0,
                                     std::min(after_s, MAX_TRANSITION_S));
    cmd.timer_action = ScheduleCommand::TimerAction::Start;
    cmd.timer = {.after_s = static_cast<uint32_t>(after_s), .fade_s = static_cast<uint32_t>(fade_s)};
}

/** Valid days only. Anything else in the list is ignored. */
uint8_t parse_days(const cJSON* days) {
    uint8_t mask = 0;
    const cJSON* day = nullptr;
    cJSON_ArrayForEach(day, days) {
        const auto value = as_number(day);
        if (value && *value >= 0.0 && *value <= 6.0)
            mask |= static_cast<uint8_t>(1U << static_cast<int>(*value));
    }
    return mask;
}

Alarm parse_alarm(const cJSON* json, Alarm alarm) {
    const cJSON* enabled = cJSON_GetObjectItem(json, "enabled");
    if (cJSON_IsBool(enabled)) alarm.enabled = cJSON_IsTrue(enabled);
    if (const auto hour = as_number(cJSON_GetObjectItem(json, "hour")))
        alarm.hour = static_cast<uint8_t>(std::clamp(*hour, 0.0, 23.0));
    if (const auto minute = as_number(cJSON_GetObjectItem(json, "minute")))
        alarm.minute = static_cast<uint8_t>(std::clamp(*minute, 0.0, 59.0));
    const cJSON* days = cJSON_GetObjectItem(json, "days");
    if (cJSON_IsArray(days)) alarm.days = parse_days(days);
    if (const auto rise = as_number(cJSON_GetObjectItem(json, "rise")))
        alarm.rise_s = static_cast<uint16_t>(std::clamp(*rise, MIN_RISE_S, MAX_RISE_S));
    if (const auto brightness = as_number(cJSON_GetObjectItem(json, "brightness")))
        alarm.brightness = static_cast<uint8_t>(std::clamp(*brightness, 1.0, 255.0));
    if (const auto color = parse_color(cJSON_GetObjectItem(json, "color"))) alarm.color = *color;
    return alarm;
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

std::optional<ScheduleCommand> parse_schedule_command(std::string_view json, const Alarm& current) {
    const JsonPtr root{cJSON_ParseWithLength(json.data(), json.size())};
    if (!cJSON_IsObject(root.get())) return std::nullopt;

    ScheduleCommand cmd;
    parse_timer(cJSON_GetObjectItem(root.get(), "timer"), cmd);
    const cJSON* alarm = cJSON_GetObjectItem(root.get(), "alarm");
    if (cJSON_IsObject(alarm)) cmd.alarm = parse_alarm(alarm, current);
    return cmd;
}

std::optional<DebugCommand> parse_debug_command(std::string_view json) {
    const JsonPtr root{cJSON_ParseWithLength(json.data(), json.size())};
    if (!cJSON_IsObject(root.get())) return std::nullopt;

    const cJSON* cmd = cJSON_GetObjectItem(root.get(), "cmd");
    if (!cJSON_IsString(cmd) || std::string_view{cmd->valuestring} != "ota") return std::nullopt;
    return DebugCommand{.kind = DebugCommand::Kind::Ota,
                        .force = cJSON_IsTrue(cJSON_GetObjectItem(root.get(), "force")) != 0};
}

InfoJson format_info(const DeviceInfo& info) {
    // An image's version field holds at most this many characters, so nothing here can overflow.
    constexpr std::size_t MAX_VERSION = 31;
    const std::string_view version = info.version.substr(0, MAX_VERSION);
    const std::string_view failed = info.rolled_back.substr(0, MAX_VERSION);

    std::array<char, MAX_VERSION + 3> rolled_back{"null"};  // room for the quotes and NUL
    if (!failed.empty()) {
        (void)std::snprintf(rolled_back.data(), rolled_back.size(), R"("%.*s")",
                            static_cast<int>(failed.size()), failed.data());
    }

    InfoJson out;
    const int n =
        std::snprintf(out.buf_.data(), out.buf_.size(),
                      R"({"version":"%.*s","confirmed":%s,"rolled_back":%s,"ota":"%s","progress":%u})",
                      static_cast<int>(version.size()), version.data(), info.confirmed ? "true" : "false",
                      rolled_back.data(), info.ota, info.progress);
    out.len_ = static_cast<std::size_t>(n);
    return out;
}

ScheduleJson format_schedule(bool time_synced, const std::optional<TimerStatus>& timer, const Alarm& alarm) {
    std::array<char, 48> timer_json{"null"};
    if (timer) {
        (void)std::snprintf(timer_json.data(), timer_json.size(), R"({"ends":%lld,"fade":%lu})",
                            static_cast<long long>(timer->ends), static_cast<unsigned long>(timer->fade_s));
    }

    std::array<char, 14> days{};  // "0,1,2,3,4,5,6" and its NUL
    std::size_t used = 0;
    for (int day = 0; day < 7; ++day) {
        if ((alarm.days & (1U << day)) == 0) continue;
        if (used > 0) days[used++] = ',';
        days[used++] = static_cast<char>('0' + day);
    }

    ScheduleJson out;
    const int n = std::snprintf(
        out.buf_.data(), out.buf_.size(),
        R"({"time_synced":%s,"timer":%s,"alarm":{"enabled":%s,"hour":%u,"minute":%u,"days":[%s],"rise":%u,"brightness":%u,"color":{"r":%u,"g":%u,"b":%u}}})",
        time_synced ? "true" : "false", timer_json.data(), alarm.enabled ? "true" : "false", alarm.hour,
        alarm.minute, days.data(), alarm.rise_s, alarm.brightness, alarm.color.r, alarm.color.g,
        alarm.color.b);
    out.len_ = static_cast<std::size_t>(n);
    return out;
}
