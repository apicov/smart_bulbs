#pragma once
#include <chrono>
#include <string_view>
#include "BulbProtocol.hpp"
#include "Callback.hpp"
#include "LightEngine.hpp"
#include "Mutex.hpp"
#include "StaticTask.hpp"
#include "esp_err.h"
#include "esp_timer.h"

/**
 * Owns the bulb's LightState, sleep timer and alarm: applies JSON commands,
 * persists to NVS and reports both as JSON. See BulbProtocol.hpp for the formats.
 */
class BulbCtrl {
public:
    using JsonListener = Callback<void(std::string_view json)>;

    explicit BulbCtrl(LightEngine& engine) : engine_{engine} {}
    BulbCtrl(const BulbCtrl&) = delete;
    BulbCtrl& operator=(const BulbCtrl&) = delete;

    /** Load state and alarm from NVS (or defaults), show the state and start the schedule task. */
    [[nodiscard]] esp_err_t start(bool power_on_light, JsonListener on_state, JsonListener on_schedule);

    /** Apply a `set` command. Returns false if the payload is not valid JSON. */
    bool apply_json(std::string_view json);

    /** Apply a `schedule_set` command. Returns false if the payload is not valid JSON. */
    bool apply_schedule_json(std::string_view json);

    [[nodiscard]] StateJson state_json();
    [[nodiscard]] ScheduleJson schedule_json();

    [[nodiscard]] std::size_t stack_free_bytes() const { return task_.stack_free_bytes(); }

private:
    /** What's left to do outside the lock after applying a state. */
    struct Applied {
        bool changed = false;
        bool schedule_changed = false;
        StateJson report;
    };

    static constexpr const char* TAG = "bulb_ctrl";
    static constexpr std::chrono::milliseconds BOOT_FADE{1000};
    static constexpr std::chrono::microseconds SAVE_DELAY = std::chrono::seconds{2};
    static constexpr std::size_t TASK_STACK_BYTES = 4096;
    static constexpr UBaseType_t TASK_PRIORITY = 4;

    LightEngine& engine_;
    JsonListener on_state_;
    JsonListener on_schedule_;
    rtos::Mutex mtx_;  // guards the members up to and including rise_active_
    LightState state_{};
    Alarm alarm_{};
    bool timer_active_ = false;
    std::chrono::microseconds timer_fade_start_{0};  // on the monotonic esp_timer clock
    uint32_t timer_fade_s_ = 0;
    bool rise_active_ = false;  // stops one alarm starting twice
    esp_timer_handle_t save_timer_ = nullptr;
    bool clock_valid_ = false;  // only touched by the schedule task
    rtos::StaticTask<TASK_STACK_BYTES> task_;

    [[nodiscard]] Applied apply_locked(const LightState& next, std::chrono::milliseconds transition);
    void finish_apply(const Applied& applied);
    [[nodiscard]] ScheduleJson schedule_json_locked();
    void publish_schedule();

    [[noreturn]] void schedule_loop();
    void check_clock();
    void check_sleep_timer();
    void check_alarm();

    void load();
    void save();
    static void save_alarm(const Alarm& alarm);
};
