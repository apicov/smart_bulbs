#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace rtos {

/**
 * FreeRTOS mutex with static storage, for use with std::lock_guard.
 *
 * The constructor calls into FreeRTOS, so create it after the scheduler has
 * started rather than as a namespace-scope global.
 */
class Mutex {
public:
    Mutex() : handle_{xSemaphoreCreateMutexStatic(&storage_)} {}
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;

    void lock() { xSemaphoreTake(handle_, portMAX_DELAY); }
    bool try_lock() { return xSemaphoreTake(handle_, 0) == pdTRUE; }
    void unlock() { xSemaphoreGive(handle_); }

private:
    StaticSemaphore_t storage_{};
    SemaphoreHandle_t handle_;
};

}  // namespace rtos
