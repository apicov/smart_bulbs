#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>

#include "BulbProtocol.hpp"
#include "Callback.hpp"
#include "LightModel.hpp"

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
    test_callback();

    if (failures > 0) {
        std::printf("%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("All host tests passed");
    return EXIT_SUCCESS;
}
