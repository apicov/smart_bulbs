#include "Pir.hpp"
#include <cstdint>
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_log.h"

esp_err_t Pir::start(gpio_num_t pin, MotionCallback on_change) {
    pin_ = pin;
    on_change_ = on_change;

    gpio_config_t io = {};
    io.pin_bit_mask = uint64_t{1} << pin_;
    io.mode = GPIO_MODE_INPUT;
    io.pull_down_en = GPIO_PULLDOWN_ENABLE;  // HC-SR505 drives high on motion
    io.intr_type = GPIO_INTR_ANYEDGE;
    ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "gpio_config");

    state_.store(gpio_get_level(pin_) != 0);
    task_.start<&Pir::loop>("pir", TASK_PRIORITY, this);

    const esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_ERR_INVALID_STATE)  // already installed is fine
        ESP_RETURN_ON_ERROR(err, TAG, "gpio_install_isr_service");
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(pin_, isr, this), TAG, "gpio_isr_handler_add");

    ESP_LOGI(TAG, "PIR on GPIO%d", pin_);
    return ESP_OK;
}

void IRAM_ATTR Pir::isr(void* self) {
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(static_cast<Pir*>(self)->task_.handle(), &woken);
    portYIELD_FROM_ISR(woken);
}

void Pir::loop() {
    for (;;) {
        // Wake on an edge, or periodically as a safety net against a missed interrupt.
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(SAFETY_POLL_MS));
        vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));
        ulTaskNotifyTake(pdTRUE, 0);  // drop edges that arrived while debouncing

        const bool level = gpio_get_level(pin_) != 0;
        if (level != state_.load()) {
            state_.store(level);
            ESP_LOGI(TAG, "motion %s", level ? "detected" : "cleared");
            if (on_change_) on_change_(level);
        }
    }
}
