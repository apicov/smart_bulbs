#pragma once
#include <atomic>
#include <cstddef>
#include "Callback.hpp"
#include "StaticTask.hpp"
#include "driver/gpio.h"
#include "esp_err.h"

/**
 * Active-high PIR sensor (e.g. HC-SR505) on one GPIO.
 *
 * An edge interrupt wakes a task which debounces the line and invokes the
 * callback (from that task, not the ISR) whenever the motion state changes.
 */
class Pir {
public:
    using MotionCallback = Callback<void(bool motion)>;

    Pir() = default;
    Pir(const Pir&) = delete;
    Pir& operator=(const Pir&) = delete;

    [[nodiscard]] esp_err_t start(gpio_num_t pin, MotionCallback on_change);

    /** Safe to call from any task. */
    [[nodiscard]] bool motion() const { return state_.load(); }

    [[nodiscard]] std::size_t stack_free_bytes() const { return task_.stack_free_bytes(); }

private:
    static constexpr const char* TAG = "pir";
    static constexpr int DEBOUNCE_MS = 50;
    static constexpr int SAFETY_POLL_MS = 5000;
    static constexpr std::size_t TASK_STACK_BYTES = 3072;
    static constexpr UBaseType_t TASK_PRIORITY = 5;

    gpio_num_t pin_ = GPIO_NUM_NC;
    MotionCallback on_change_;
    std::atomic<bool> state_{false};
    rtos::StaticTask<TASK_STACK_BYTES> task_;

    static void isr(void* self);
    [[noreturn]] void loop();
};
