#include "OtaUpdater.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"

namespace {
constexpr int HTTP_TIMEOUT_MS = 15000;
constexpr int HTTP_BUFFER_BYTES = 2048;
constexpr uint8_t PROGRESS_STEP = 10;  // percent between reports
constexpr TickType_t PROVE_PERIOD = pdMS_TO_TICKS(1000);
constexpr TickType_t WAIT_PERIOD = pdMS_TO_TICKS(1000);
constexpr TickType_t REPORT_TIME = pdMS_TO_TICKS(1000);  // for the last message to leave

std::chrono::seconds since_boot() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::microseconds{esp_timer_get_time()});
}

bool running_image_unproved() {
    esp_ota_img_states_t state{};
    return esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK &&
           state == ESP_OTA_IMG_PENDING_VERIFY;
}
}  // namespace

void OtaUpdater::start(const char* url, Hooks hooks) {
    url_ = url;
    hooks_ = hooks;
    confirmed_.store(!running_image_unproved());

    if (const esp_partition_t* failed = esp_ota_get_last_invalid_partition()) {
        esp_app_desc_t desc{};
        if (esp_ota_get_partition_description(failed, &desc) == ESP_OK) {
            (void)std::snprintf(rolled_back_.data(), rolled_back_.size(), "%s", desc.version);
            ESP_LOGW(TAG, "Version %s failed here and was rolled back", rolled_back_.data());
        }
    }
    task_.start<&OtaUpdater::loop>("ota", TASK_PRIORITY, this);
}

void OtaUpdater::request(bool force) {
    if (working_.exchange(true)) {
        ESP_LOGW(TAG, "Already updating");
        return;
    }
    force_.store(force);
    xTaskNotifyGive(task_.handle());
}

std::string_view OtaUpdater::running_version() { return esp_app_get_description()->version; }

void OtaUpdater::loop() {
    if (!confirmed_.load()) prove_image();
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        update(force_.load());
        working_.store(false);
    }
}

void OtaUpdater::prove_image() {
    ESP_LOGW(TAG, "New image: keeping it after %lld s connected to MQTT",
             static_cast<long long>(CONFIRM_AFTER_CONNECTED.count()));
    std::chrono::seconds connected_since{0};
    for (;;) {
        if (ulTaskNotifyTake(pdTRUE, PROVE_PERIOD) > 0) {  // esp_ota_begin would refuse too
            set_status(OtaStatus::Unconfirmed);
            working_.store(false);
        }

        const auto now = since_boot();
        if (!hooks_.connected()) connected_since = now;

        switch (confirm_action(now - connected_since, now)) {
        case ConfirmAction::Wait:
            break;
        case ConfirmAction::Confirm: {
            const esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
            if (err != ESP_OK) {  // still rolled back at the next restart
                ESP_LOGE(TAG, "Can't keep this image: %s", esp_err_to_name(err));
                return;
            }
            ESP_LOGI(TAG, "Image confirmed");
            confirmed_.store(true);
            hooks_.on_status();
            return;
        }
        case ConfirmAction::RollBack: {
            ESP_LOGE(TAG, "No MQTT connection in time: going back to the previous image");
            hooks_.before_restart();
            const esp_err_t err = esp_ota_mark_app_invalid_rollback_and_reboot();
            // Only returns if there's nothing to go back to. Better to keep running.
            ESP_LOGE(TAG, "Can't roll back: %s", esp_err_to_name(err));
            return;
        }
        }
    }
}

void OtaUpdater::update(bool force) {
    // Restarting would forget a running sleep timer and could leave a light on all night.
    if (hooks_.busy()) {
        set_status(OtaStatus::Busy);
        return;
    }
    if (!download(force)) return;

    // Started during the download, or just now: it's safe to go ahead when it ends.
    if (hooks_.busy()) {
        set_status(OtaStatus::Waiting);
        while (hooks_.busy()) vTaskDelay(WAIT_PERIOD);
    }
    set_status(OtaStatus::Restarting);
    vTaskDelay(REPORT_TIME);
    hooks_.before_restart();
    esp_restart();
}

bool OtaUpdater::download(bool force) {
    set_status(OtaStatus::Checking);
    set_progress(0);

    esp_http_client_config_t http{};
    http.url = url_;
    http.crt_bundle_attach = esp_crt_bundle_attach;
    http.timeout_ms = HTTP_TIMEOUT_MS;
    http.buffer_size = HTTP_BUFFER_BYTES;
    esp_https_ota_config_t config{};
    config.http_config = &http;

    esp_https_ota_handle_t ota = nullptr;
    esp_err_t err = esp_https_ota_begin(&config, &ota);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Can't get %s: %s", url_, esp_err_to_name(err));
        set_status(OtaStatus::Failed);
        return false;
    }

    esp_app_desc_t offered{};
    err = esp_https_ota_get_img_desc(ota, &offered);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Can't read the image header: %s", esp_err_to_name(err));
        (void)esp_https_ota_abort(ota);
        set_status(err == ESP_ERR_OTA_VALIDATE_FAILED ? OtaStatus::Rejected : OtaStatus::Failed);
        return false;
    }

    const OtaDecision decision = decide_ota(offered.version, running_version(), rolled_back_version(), force);
    if (decision != OtaDecision::Install) {
        ESP_LOGI(TAG, "Not installing %s", offered.version);
        (void)esp_https_ota_abort(ota);
        set_status(decision == OtaDecision::UpToDate ? OtaStatus::UpToDate : OtaStatus::RolledBack);
        return false;
    }

    const std::string_view running = running_version();
    ESP_LOGI(TAG, "Installing %s over %.*s", offered.version, static_cast<int>(running.size()),
             running.data());
    set_status(OtaStatus::Downloading);
    const int size = esp_https_ota_get_image_size(ota);
    while ((err = esp_https_ota_perform(ota)) == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
        if (size > 0) {
            const auto percent = static_cast<uint8_t>(
                static_cast<int64_t>(esp_https_ota_get_image_len_read(ota)) * 100 / size);
            if (percent >= progress_.load() + PROGRESS_STEP) set_progress(percent);
        }
        vTaskDelay(1);  // let the idle task run, whatever the download's speed
    }

    if (err != ESP_OK || !esp_https_ota_is_complete_data_received(ota)) {
        ESP_LOGE(TAG, "Download failed: %s", esp_err_to_name(err));
        (void)esp_https_ota_abort(ota);
        set_status(err == ESP_ERR_OTA_VALIDATE_FAILED ? OtaStatus::Rejected : OtaStatus::Failed);
        return false;
    }

    // Checks the signature, and only then selects the image for the next boot.
    err = esp_https_ota_finish(ota);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Image not installed: %s", esp_err_to_name(err));
        set_status(err == ESP_ERR_OTA_VALIDATE_FAILED ? OtaStatus::Rejected : OtaStatus::Failed);
        return false;
    }
    ESP_LOGI(TAG, "Installed %s", offered.version);
    set_progress(100);
    return true;
}

void OtaUpdater::set_status(OtaStatus status) {
    status_.store(status);
    ESP_LOGI(TAG, "Status: %s", ota_status_name(status));
    hooks_.on_status();
}

void OtaUpdater::set_progress(uint8_t percent) {
    progress_.store(std::min<uint8_t>(percent, 100));
    hooks_.on_status();
}
