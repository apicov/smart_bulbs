#include "Pir.hpp"
#include "esp_log.h"

esp_err_t Pir::start(gpio_num_t pin, Callback cb) {
    pin_ = pin;
    cb_ = std::move(cb);

    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << pin_;
    io.mode = GPIO_MODE_INPUT;
    io.pull_down_en = GPIO_PULLDOWN_ENABLE;  // HC-SR505 drives high on motion
    io.intr_type = GPIO_INTR_ANYEDGE;
    ESP_ERROR_CHECK(gpio_config(&io));

    xTaskCreate(task_entry, "pir", 3072, this, 5, &task_);

    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)  // already installed is fine
        return err;
    ESP_ERROR_CHECK(gpio_isr_handler_add(pin_, isr, this));

    ESP_LOGI(TAG, "PIR on GPIO%d", pin_);
    return ESP_OK;
}

void IRAM_ATTR Pir::isr(void* self) {
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(static_cast<Pir*>(self)->task_, &woken);
    portYIELD_FROM_ISR(woken);
}

void Pir::task_entry(void* self) { static_cast<Pir*>(self)->loop(); }

void Pir::loop() {
    state_ = gpio_get_level(pin_);
    for (;;) {
        // Wake on an edge, or every 5 s as a safety net against a missed interrupt.
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000));
        vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));
        ulTaskNotifyTake(pdTRUE, 0);  // drop edges that arrived while debouncing

        const bool level = gpio_get_level(pin_);
        if (level != state_) {
            state_ = level;
            ESP_LOGI(TAG, "motion %s", level ? "detected" : "cleared");
            if (cb_) cb_(level);
        }
    }
}
