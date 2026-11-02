#pragma once
#include <functional>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/**
 * Active-high PIR sensor (e.g. HC-SR505) on one GPIO.
 *
 * An edge interrupt wakes a task which debounces the line and invokes the
 * callback (from that task, not the ISR) whenever the motion state changes.
 */
class Pir {
public:
    using Callback = std::function<void(bool motion)>;

    esp_err_t start(gpio_num_t pin, Callback cb);
    bool motion() const { return state_; }

private:
    static constexpr const char* TAG = "pir";
    static constexpr int DEBOUNCE_MS = 50;

    gpio_num_t pin_ = GPIO_NUM_NC;
    Callback cb_;
    TaskHandle_t task_ = nullptr;
    volatile bool state_ = false;

    static void isr(void* self);
    static void task_entry(void* self);
    void loop();
};
