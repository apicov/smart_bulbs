#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include "BulbConfig.hpp"
#include "BulbProtocol.hpp"
#include "Callback.hpp"
#include "LightModel.hpp"
#include "OtaPolicy.hpp"

using namespace std::chrono_literals;

namespace {
int failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("%s:%d: failed: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

// The float code LightEngine used before moving to fixed point, as the reference.
Rgb reference_pixel(const LightState& s, const PowerBudget& p) {
    const float k = s.on ? s.brightness / 255.0f : 0.0f;
    const std::array<float, 3> level{s.color.r * k, s.color.g * k, s.color.b * k};
    std::array<uint8_t, 3> out{};
    for (int c = 0; c < 3; ++c) {
        const auto i = static_cast<uint8_t>(std::lround(std::clamp(level[c], 0.0f, 255.0f)));
        out[c] = static_cast<uint8_t>(std::lround(std::pow(i / 255.0, 2.2) * 255.0));
    }
    if (p.max_milliamps > 0) {
        const float ma = p.led_count * p.ma_per_channel * (out[0] + out[1] + out[2]) / 255.0f;
        if (ma > p.max_milliamps) {
            const float scale = p.max_milliamps / ma;
            for (auto& v : out) v = static_cast<uint8_t>(v * scale);
        }
    }
    return {out[0], out[1], out[2]};
}

bool within_one(uint8_t a, uint8_t b) { return std::abs(int{a} - int{b}) <= 1; }

void test_gamma() {
    for (int i = 0; i < 256; ++i)
        CHECK(GAMMA[static_cast<std::size_t>(i)] == std::lround(std::pow(i / 255.0, 2.2) * 255.0));
    CHECK(GAMMA[0] == 0);
    CHECK(GAMMA[255] == 255);
}

void test_pixel_matches_float_code() {
    constexpr std::array<PowerBudget, 4> budgets{{{24, 20, 0}, {24, 20, 450}, {24, 20, 1000}, {1024, 20, 1}}};
    for (const auto& budget : budgets)
        for (int c = 0; c < 256; c += 5)
            for (int b = 0; b < 256; b += 5) {
                const LightState s{true,
                                   {static_cast<uint8_t>(c), static_cast<uint8_t>(255 - c), 100},
                                   static_cast<uint8_t>(b)};
                const Rgb got = to_pixel(target_levels(s), budget);
                const Rgb want = reference_pixel(s, budget);
                CHECK(within_one(got.r, want.r));
                CHECK(within_one(got.g, want.g));
                CHECK(within_one(got.b, want.b));
            }
}

void test_current_limit() {
    const LightState white{true, {255, 255, 255}, 255};
    CHECK((to_pixel(target_levels(white), {24, 20, 0}) == Rgb{255, 255, 255}));

    // 24 LEDs * 3 channels * 20 mA = 1440 mA at full white: 450 mA allows 31.25%.
    const Rgb limited = to_pixel(target_levels(white), {24, 20, 450});
    CHECK((limited == Rgb{79, 79, 79}));

    const LightState off{false, {255, 255, 255}, 255};
    CHECK((to_pixel(target_levels(off), {24, 20, 450}) == Rgb{0, 0, 0}));
}

void test_fade() {
    const Levels from{0, 65025, 1000};
    const Levels to{65025, 0, 1000};
    CHECK(fade_levels(from, to, 0us, 1000us) == from);
    CHECK(fade_levels(from, to, 1000us, 1000us) == to);
    CHECK(fade_levels(from, to, 5000us, 1000us) == to);
    CHECK(fade_levels(from, to, 0us, 0us) == to);
    CHECK((fade_levels(from, to, 500us, 1000us) == Levels{32512, 32513, 1000}));

    // The longest allowed fade must not overflow.
    const std::chrono::microseconds hour = 3600s;
    CHECK((fade_levels({0, 0, 0}, {65025, 65025, 65025}, hour / 2, hour) == Levels{32512, 32512, 32512}));
}

Command parse_ok(std::string_view json, const LightState& current = {}) {
    const auto cmd = parse_command(json, current);
    CHECK(cmd.has_value());
    return cmd.value_or(Command{current, 0ms});
}

void test_parse_state() {
    const LightState on{true, {1, 2, 3}, 50};
    const LightState off{false, {1, 2, 3}, 50};
    CHECK(parse_ok(R"({"state":"OFF"})", on).state.on == false);
    CHECK(parse_ok(R"({"state":"on"})", off).state.on == true);
    CHECK(parse_ok(R"({"state":"TOGGLE"})", on).state.on == false);
    CHECK(parse_ok(R"({"state":"TOGGLE"})", off).state.on == true);
    CHECK(parse_ok(R"({"state":false})", on).state.on == false);
    CHECK(parse_ok(R"({"state":true})", off).state.on == true);
    CHECK(parse_ok(R"({"state":"nonsense"})", on).state == on);
    CHECK(parse_ok("{}", on).state == on);
}

void test_parse_brightness() {
    const LightState s{true, {1, 2, 3}, 50};
    CHECK(parse_ok(R"({"brightness":200})", s).state.brightness == 200);
    CHECK(parse_ok(R"({"brightness":999})", s).state.brightness == 255);
    CHECK(parse_ok(R"({"brightness":-5})", s).state.on == false);
    CHECK(parse_ok(R"({"brightness":1e300})", s).state.brightness == 255);

    // Zero switches off but keeps the old brightness for the next "ON".
    const LightState zero = parse_ok(R"({"brightness":0})", s).state;
    CHECK(zero.on == false);
    CHECK(zero.brightness == 50);
}

void test_parse_color() {
    const LightState s{true, {1, 2, 3}, 50};
    CHECK((parse_ok(R"({"color":{"r":10,"g":20,"b":30}})", s).state.color == Rgb{10, 20, 30}));
    CHECK((parse_ok(R"({"color":[10,20,30]})", s).state.color == Rgb{10, 20, 30}));
    CHECK((parse_ok(R"({"color":[300,-1,30]})", s).state.color == Rgb{255, 0, 30}));
    CHECK((parse_ok(R"({"color":[10,20]})", s).state.color == s.color));
    CHECK((parse_ok(R"({"color":{"r":10,"g":20}})", s).state.color == s.color));
    CHECK((parse_ok(R"({"color":[10,"x",30]})", s).state.color == s.color));
    CHECK((parse_ok(R"({"color":"red"})", s).state.color == s.color));
}

void test_parse_transition() {
    CHECK(parse_ok("{}").transition == 400ms);
    CHECK(parse_ok(R"({"transition":1.5})").transition == 1500ms);
    CHECK(parse_ok(R"({"transition":0})").transition == 0ms);
    CHECK(parse_ok(R"({"transition":-1})").transition == 0ms);
    CHECK(parse_ok(R"({"transition":99999})").transition == 3600s);
}

void test_parse_invalid() {
    const LightState s{};
    CHECK(!parse_command("", s));
    CHECK(!parse_command("not json", s));
    CHECK(!parse_command(R"({"state":)", s));
    CHECK(!parse_command("[1,2,3]", s));
    CHECK(!parse_command("42", s));

    // The payload isn't NUL-terminated on the wire: only `size` bytes may be read.
    const std::string_view truncated{R"({"state":"OFF"}garbage)", 15};
    CHECK(parse_ok(truncated, s).state.on == false);
}

void test_format_state() {
    const LightState s{true, {1, 2, 3}, 128};
    CHECK(format_state(s).view() ==
          R"({"state":"ON","brightness":128,"color_mode":"rgb","color":{"r":1,"g":2,"b":3}})");

    // The longest possible message has to fit, or it would be cut off.
    const LightState longest{false, {255, 255, 255}, 255};
    CHECK(format_state(longest).view() ==
          R"({"state":"OFF","brightness":255,"color_mode":"rgb","color":{"r":255,"g":255,"b":255}})");
    CHECK(format_state(longest).view().data()[format_state(longest).view().size()] == '\0');
}

void test_stored_layout() {
    // Bulbs in the field hold these bytes in NVS.
    const StoredState rec{.state = {true, {10, 20, 30}, 40}};
    constexpr std::array<uint8_t, 6> expected{1, 1, 10, 20, 30, 40};
    CHECK(std::memcmp(&rec, expected.data(), expected.size()) == 0);
}

void test_alarm_window() {
    Alarm alarm{
        .enabled = true, .hour = 7, .minute = 0, .days = 0b0111110, .rise_s = 1800};  // Monday to Friday
    const auto at = [](int day, int hour, int minute, int second = 0) {
        return second_of_week(day, hour, minute, second);
    };
    CHECK(!alarm_rise_remaining(alarm, at(1, 6, 29, 59)));
    CHECK(alarm_rise_remaining(alarm, at(1, 6, 30)) == 1800U);
    CHECK(alarm_rise_remaining(alarm, at(1, 6, 59, 59)) == 1U);
    CHECK(!alarm_rise_remaining(alarm, at(1, 7, 0)));   // reached its alarm time
    CHECK(!alarm_rise_remaining(alarm, at(0, 6, 45)));  // Sunday isn't selected
    CHECK(alarm_rise_remaining(alarm, at(5, 6, 45)) == 900U);
    CHECK(!alarm_rise_remaining(alarm, at(6, 6, 45)));

    // Once: any day, and only while enabled.
    alarm.days = 0;
    CHECK(alarm_rise_remaining(alarm, at(0, 6, 45)) == 900U);
    alarm.enabled = false;
    CHECK(!alarm_rise_remaining(alarm, at(0, 6, 45)));

    // A rise for 00:10 Monday starts on Sunday evening.
    const Alarm midnight{.enabled = true, .hour = 0, .minute = 10, .days = 0b0000010, .rise_s = 1800};
    CHECK(alarm_rise_remaining(midnight, at(0, 23, 50)) == 1200U);
    CHECK(alarm_rise_remaining(midnight, at(1, 0, 0)) == 600U);

    // A rise for 00:10 Sunday starts on Saturday evening, across the end of the week.
    const Alarm week{.enabled = true, .hour = 0, .minute = 10, .days = 0b0000001, .rise_s = 1800};
    CHECK(alarm_rise_remaining(week, at(6, 23, 50)) == 1200U);
    CHECK(alarm_rise_remaining(week, at(0, 0, 5)) == 300U);
    CHECK(!alarm_rise_remaining(week, at(5, 23, 50)));
}

ScheduleCommand parse_schedule_ok(std::string_view json, const Alarm& current = {}) {
    const auto cmd = parse_schedule_command(json, current);
    CHECK(cmd.has_value());
    return cmd.value_or(ScheduleCommand{});
}

void test_parse_timer() {
    using enum ScheduleCommand::TimerAction;
    CHECK(parse_schedule_ok("{}").timer_action == None);
    CHECK(parse_schedule_ok(R"({"timer":null})").timer_action == Cancel);
    CHECK(parse_schedule_ok(R"({"timer":{"after":0}})").timer_action == Cancel);
    CHECK(parse_schedule_ok(R"({"timer":{"fade":5}})").timer_action == None);  // no duration

    const auto sudden = parse_schedule_ok(R"({"timer":{"after":1800}})");
    CHECK(sudden.timer_action == Start);
    CHECK(sudden.timer.after_s == 1800);
    CHECK(sudden.timer.fade_s == 0);

    const auto fade = parse_schedule_ok(R"({"timer":{"after":1800,"fade":60}})");
    CHECK(fade.timer.after_s == 1800);
    CHECK(fade.timer.fade_s == 60);

    // A fade can't outlast the timer, or an hour.
    CHECK(parse_schedule_ok(R"({"timer":{"after":30,"fade":60}})").timer.fade_s == 30);
    CHECK(parse_schedule_ok(R"({"timer":{"after":86400,"fade":7200}})").timer.fade_s == 3600);
    CHECK(parse_schedule_ok(R"({"timer":{"after":999999}})").timer.after_s == 86400);
    CHECK(parse_schedule_ok(R"({"timer":{"after":60,"fade":-5}})").timer.fade_s == 0);
}

void test_parse_alarm() {
    CHECK(!parse_schedule_ok("{}").alarm);
    CHECK(!parse_schedule_ok(R"({"alarm":null})").alarm);

    const auto full = parse_schedule_ok(
        R"({"alarm":{"enabled":true,"hour":6,"minute":45,"days":[1,5],"rise":600,"brightness":200,"color":{"r":1,"g":2,"b":3}}})");
    const Alarm want{.enabled = true,
                     .hour = 6,
                     .minute = 45,
                     .days = 0b0100010,
                     .rise_s = 600,
                     .brightness = 200,
                     .color = {1, 2, 3}};
    CHECK(full.alarm == want);

    // Only the fields sent change.
    const auto partial = parse_schedule_ok(R"({"alarm":{"enabled":false}})", want);
    Alarm disabled = want;
    disabled.enabled = false;
    CHECK(partial.alarm == disabled);

    // An empty list means once, and bad days are ignored.
    CHECK(parse_schedule_ok(R"({"alarm":{"days":[]}})", want).alarm->days == 0);
    CHECK(parse_schedule_ok(R"({"alarm":{"days":[0,7,-1,"x",6]}})").alarm->days == 0b1000001);

    // Out of range values are clamped.
    const auto clamped = parse_schedule_ok(R"({"alarm":{"hour":99,"minute":-3,"rise":1,"brightness":0}})");
    CHECK(clamped.alarm->hour == 23);
    CHECK(clamped.alarm->minute == 0);
    CHECK(clamped.alarm->rise_s == 5);
    CHECK(clamped.alarm->brightness == 1);
    CHECK(parse_schedule_ok(R"({"alarm":{"rise":99999}})").alarm->rise_s == 3600);

    CHECK(!parse_schedule_command("nonsense", Alarm{}));
    CHECK(!parse_schedule_command("[]", Alarm{}));
}

void test_format_schedule() {
    const Alarm off{};
    CHECK(
        format_schedule(false, std::nullopt, off).view() ==
        R"({"time_synced":false,"timer":null,"alarm":{"enabled":false,"hour":7,"minute":0,"days":[],"rise":1800,"brightness":255,"color":{"r":255,"g":180,"b":100}}})");

    const Alarm weekdays{.enabled = true, .hour = 6, .minute = 30, .days = 0b0111110, .rise_s = 600};
    CHECK(
        format_schedule(true, TimerStatus{.ends = 1800000000, .fade_s = 60}, weekdays).view() ==
        R"({"time_synced":true,"timer":{"ends":1800000000,"fade":60},"alarm":{"enabled":true,"hour":6,"minute":30,"days":[1,2,3,4,5],"rise":600,"brightness":255,"color":{"r":255,"g":180,"b":100}}})");

    // The longest possible message has to fit, or it would be cut off.
    const Alarm longest{.enabled = false,
                        .hour = 23,
                        .minute = 59,
                        .days = 0b1111111,
                        .rise_s = 3600,
                        .brightness = 255,
                        .color = {255, 255, 255}};
    const ScheduleJson json = format_schedule(false, TimerStatus{.ends = INT64_MAX, .fade_s = 3600}, longest);
    CHECK(json.view().size() < 256);
    CHECK(json.view().back() == '}');
    CHECK(json.view().data()[json.view().size()] == '\0');
}

void test_stored_alarm_layout() {
    // Bulbs will hold these bytes in NVS, so this mustn't change without a new version.
    const StoredAlarm rec{.alarm = {.enabled = true,
                                    .hour = 7,
                                    .minute = 30,
                                    .days = 0b0111110,
                                    .rise_s = 0x0102,
                                    .brightness = 200,
                                    .color = {10, 20, 30}}};
    const auto* bytes = reinterpret_cast<const uint8_t*>(&rec);
    constexpr std::array<std::size_t, 10> offsets{0, 2, 3, 4, 5, 6, 8, 9, 10, 11};
    constexpr std::array<uint8_t, 10> values{1, 1, 7, 30, 0b0111110, 0x02, 200, 10, 20, 30};
    for (std::size_t i = 0; i < offsets.size(); ++i) CHECK(bytes[offsets[i]] == values[i]);
    CHECK(bytes[7] == 0x01);
}

template <std::size_t N>
void set_text(std::array<char, N>& field, std::string_view text) {
    field.fill('\0');
    std::copy(text.begin(), text.end(), field.begin());
}

BulbConfig good_config() {
    BulbConfig config;
    set_text(config.id, "living-room-2");
    set_text(config.wifi_ssid, "my-wifi");
    set_text(config.wifi_password, "12345678");
    set_text(config.mqtt_uri, "mqtts://mqtt.example.com:8883");
    set_text(config.mqtt_password, "secret");
    config.led_count = 24;
    return config;
}

void test_config_validation() {
    CHECK(config_problem(good_config()) == nullptr);

    const auto with = [](auto change) {
        BulbConfig config = good_config();
        change(config);
        return config_problem(config);
    };
    CHECK(with([](BulbConfig& c) { set_text(c.id, ""); }) != nullptr);
    CHECK(with([](BulbConfig& c) { set_text(c.id, "Living"); }) != nullptr);  // upper case
    CHECK(with([](BulbConfig& c) { set_text(c.id, "a/b"); }) != nullptr);     // would change the topic
    CHECK(with([](BulbConfig& c) { set_text(c.id, "a#"); }) != nullptr);      // an MQTT wildcard
    CHECK(with([](BulbConfig& c) { set_text(c.wifi_ssid, ""); }) != nullptr);
    CHECK(with([](BulbConfig& c) { set_text(c.wifi_password, "1234567"); }) != nullptr);
    CHECK(with([](BulbConfig& c) { set_text(c.mqtt_uri, "https://x"); }) != nullptr);
    CHECK(with([](BulbConfig& c) { set_text(c.mqtt_uri, "mqtt://x"); }) == nullptr);
    CHECK(with([](BulbConfig& c) { set_text(c.mqtt_password, ""); }) != nullptr);
    CHECK(with([](BulbConfig& c) { c.led_count = 0; }) != nullptr);
    CHECK(with([](BulbConfig& c) { c.led_count = 1025; }) != nullptr);
    CHECK(with([](BulbConfig& c) { c.led_count = 1024; }) == nullptr);
}

void test_ota_decision() {
    using enum OtaDecision;
    CHECK(decide_ota("v2", "v1", "", false) == Install);
    CHECK(decide_ota("v1", "v2", "", false) == Install);  // going back to an older version is allowed
    CHECK(decide_ota("v1", "v1", "", false) == UpToDate);
    CHECK(decide_ota("v2", "v1", "v2", false) == RolledBack);
    CHECK(decide_ota("v2", "v1", "v2", true) == Install);
    CHECK(decide_ota("v3", "v1", "v2", false) == Install);
    CHECK(decide_ota("v1", "v1", "v1", true) == UpToDate);  // force doesn't reinstall what's running
}

void test_ota_confirm() {
    using enum ConfirmAction;
    CHECK(confirm_action(0s, 0s) == Wait);
    CHECK(confirm_action(29s, 29s) == Wait);
    CHECK(confirm_action(30s, 40s) == Confirm);
    CHECK(confirm_action(0s, 299s) == Wait);
    CHECK(confirm_action(0s, 300s) == RollBack);
    CHECK(confirm_action(29s, 300s) == RollBack);
    CHECK(confirm_action(30s, 300s) == Confirm);  // proved itself at the last moment
}

void test_parse_debug_command() {
    const auto plain = parse_debug_command(R"({"cmd":"ota"})");
    CHECK(plain && plain->kind == DebugCommand::Kind::Ota && !plain->force);
    const auto forced = parse_debug_command(R"({"cmd":"ota","force":true})");
    CHECK(forced && forced->force);
    CHECK(!parse_debug_command(R"({"cmd":"ota","force":1})")->force);  // only an explicit true forces

    CHECK(!parse_debug_command(""));
    CHECK(!parse_debug_command("ota"));
    CHECK(!parse_debug_command("[]"));
    CHECK(!parse_debug_command("{}"));
    CHECK(!parse_debug_command(R"({"cmd":1})"));
    CHECK(!parse_debug_command(R"({"cmd":"reboot"})"));

    const auto logs_on = parse_debug_command(R"({"cmd":"logs","on":true})");
    CHECK(logs_on && logs_on->kind == DebugCommand::Kind::Logs && logs_on->on);
    const auto logs_off = parse_debug_command(R"({"cmd":"logs","on":false})");
    CHECK(logs_off && logs_off->kind == DebugCommand::Kind::Logs && !logs_off->on);
    CHECK(!parse_debug_command(R"({"cmd":"logs"})"));  // has to say which
    CHECK(!parse_debug_command(R"({"cmd":"logs","on":1})"));
    const auto dump = parse_debug_command(R"({"cmd":"coredump"})");
    CHECK(dump && dump->kind == DebugCommand::Kind::Coredump);
    const auto erase = parse_debug_command(R"({"cmd":"coredump_erase"})");
    CHECK(erase && erase->kind == DebugCommand::Kind::CoredumpErase);

    // A message can't choose where an update comes from: extra fields change nothing.
    const auto with_url = parse_debug_command(R"({"cmd":"ota","url":"https://evil.example/x.bin"})");
    CHECK(with_url && !with_url->force);
}

void test_format_info() {
    const auto text = [](const DeviceInfo& info) { return std::string{format_info(info).view()}; };
    const TaskStacks stack{.light = 1, .pir = 2, .schedule = 3, .ota = 4, .debug = 5};
    CHECK(
        text({.version = "abc1234",
              .reset = "power_on",
              .uptime_s = 60,
              .confirmed = true,
              .rolled_back = {},
              .ota = "idle",
              .progress = 0,
              .logs = false,
              .heap_min = 1000,
              .stack = stack,
              .crash = std::nullopt}) ==
        R"({"version":"abc1234","reset":"power_on","uptime":60,"confirmed":true,"rolled_back":null,"ota":"idle","progress":0,"logs":false,"heap_min":1000,"stack":{"light":1,"pir":2,"schedule":3,"ota":4,"debug":5},"crash":null})");

    CrashSummary crash;
    (void)std::snprintf(crash.task.data(), crash.task.size(), "main");
    (void)std::snprintf(crash.elf.data(), crash.elf.size(), "760585b2");
    crash.pc = 0x42001234;
    crash.ra = 0x420000ff;
    crash.cause = 2;
    crash.value = 7;
    CHECK(
        text({.version = "v2",
              .reset = "crash",
              .uptime_s = 1,
              .confirmed = false,
              .rolled_back = "v3",
              .ota = "downloading",
              .progress = 40,
              .logs = true,
              .heap_min = 1,
              .stack = stack,
              .crash = crash})
            .ends_with(
                R"("crash":{"task":"main","pc":"0x42001234","ra":"0x420000ff","cause":2,"value":7,"elf":"760585b2"}})"));

    // The longest possible message fits.
    const std::string longest(31, 'x');
    CrashSummary big;
    big.task.fill('t');
    big.task.back() = '\0';
    big.elf.fill('e');
    big.elf.back() = '\0';
    big.pc = big.ra = big.cause = big.value = 0xFFFFFFFF;
    const unsigned most = 0xFFFFFFFF;
    const std::string json = text({.version = longest,
                                   .reset = "power_on",
                                   .uptime_s = 0xFFFFFFFF,
                                   .confirmed = false,
                                   .rolled_back = longest,
                                   .ota = "unconfirmed",
                                   .progress = 100,
                                   .logs = false,
                                   .heap_min = 0xFFFFFFFF,
                                   .stack = {most, most, most, most, most},
                                   .crash = big});
    CHECK(json.ends_with("}}"));
    CHECK(json.size() < 512);
    CHECK(std::string_view{ota_status_name(OtaStatus::Rejected)} == "rejected");
}

void test_callback() {
    Callback<int(int)> empty;
    CHECK(!empty);

    int base = 10;
    int* p = &base;
    Callback<int(int)> add = [p](int x) { return *p + x; };
    CHECK(static_cast<bool>(add));
    CHECK(add(5) == 15);

    Callback<int(int)> copy = add;
    base = 20;
    CHECK(copy(5) == 25);

    Callback<int(int)> plain = +[](int x) { return x * 2; };
    CHECK(plain(4) == 8);
}
}  // namespace

int main() {
    test_gamma();
    test_pixel_matches_float_code();
    test_current_limit();
    test_fade();
    test_parse_state();
    test_parse_brightness();
    test_parse_color();
    test_parse_transition();
    test_parse_invalid();
    test_format_state();
    test_stored_layout();
    test_alarm_window();
    test_parse_timer();
    test_parse_alarm();
    test_format_schedule();
    test_stored_alarm_layout();
    test_config_validation();
    test_ota_decision();
    test_ota_confirm();
    test_parse_debug_command();
    test_format_info();
    test_callback();

    if (failures > 0) {
        std::printf("%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("All host tests passed");
    return EXIT_SUCCESS;
}
