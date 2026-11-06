#pragma once
#include <array>
#include <cstddef>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace rtos {

/**
 * FreeRTOS task whose stack and control block live inside this object, so
 * starting it cannot fail for lack of heap and its RAM cost shows up at link
 * time.
 */
template <std::size_t StackBytes>
class StaticTask {
public:
    StaticTask() = default;
    StaticTask(const StaticTask&) = delete;
    StaticTask& operator=(const StaticTask&) = delete;

    /** Run `(self->*Method)()` in a new task. Method must never return. */
    template <auto Method, class T>
    void start(const char* name, UBaseType_t priority, T* self) {
        handle_ = xTaskCreateStatic([](void* arg) { (static_cast<T*>(arg)->*Method)(); }, name, stack_.size(),
                                    self, priority, stack_.data(), &tcb_);
    }

    [[nodiscard]] TaskHandle_t handle() const { return handle_; }

    /** The least free stack the task has ever had, in bytes. 0 if it hasn't started. */
    [[nodiscard]] std::size_t stack_free_bytes() const {
        return handle_ ? uxTaskGetStackHighWaterMark(handle_) * sizeof(StackType_t) : 0;
    }

private:
    std::array<StackType_t, StackBytes / sizeof(StackType_t)> stack_{};
    StaticTask_t tcb_{};
    TaskHandle_t handle_ = nullptr;
};

}  // namespace rtos
