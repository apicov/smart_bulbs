#include "RemoteDebug.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <mutex>
#include "esp_core_dump.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_timer.h"

namespace {
constexpr int64_t LOG_SESSION_US = 3600LL * 1000 * 1000;
constexpr int64_t REPORT_PERIOD_US = 300LL * 1000 * 1000;
constexpr TickType_t LOOP_PERIOD = pdMS_TO_TICKS(100);
constexpr TickType_t DUMP_CHUNK_GAP = pdMS_TO_TICKS(30);

/** The dump comes from flash after a crash, and its text goes into JSON: keep only safe characters. */
template <std::size_t N>
void copy_safe(std::array<char, N>& out, const char* text, std::size_t text_size) {
    std::size_t used = 0;
    for (std::size_t i = 0; i < text_size && used + 1 < N && text[i] != '\0'; ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        const bool safe = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                          c == '_' || c == '-';
        out[used++] = safe ? static_cast<char>(c) : '?';
    }
    out[used] = '\0';
}

void put_u32(char* out, uint32_t value) {
    for (int i = 3; i >= 0; --i) {
        out[i] = static_cast<char>(value & 0xFFU);
        value >>= 8U;
    }
}
}  // namespace

void RemoteDebug::start(Hooks hooks) {
    hooks_ = hooks;
    ring_ = xRingbufferCreateStatic(ring_storage_.size(), RINGBUF_TYPE_NOSPLIT, ring_storage_.data(),
                                    &ring_control_);
    instance_ = this;
    original_log_ = esp_log_set_vprintf(&RemoteDebug::log_hook);
    task_.start<&RemoteDebug::loop>("debug", TASK_PRIORITY, this);
}

void RemoteDebug::set_logs(bool on) {
    logs_since_us_.store(esp_timer_get_time());
    logs_on_.store(on);
    xTaskNotifyGive(task_.handle());
}

void RemoteDebug::request_dump() {
    dump_requested_.store(true);
    xTaskNotifyGive(task_.handle());
}

void RemoteDebug::request_erase() {
    erase_requested_.store(true);
    xTaskNotifyGive(task_.handle());
}

std::optional<CrashSummary> RemoteDebug::crash() const {
    if (!has_crash_.load()) return std::nullopt;
    return crash_;
}

int RemoteDebug::log_hook(const char* format, va_list args) {
    RemoteDebug* self = instance_;
    if (self->logs_on_.load()) {
        va_list copy;
        va_copy(copy, args);
        self->capture(format, copy);
        va_end(copy);
    }
    return self->original_log_(format, args);
}

void RemoteDebug::capture(const char* format, va_list args) {
    // Its own task doesn't capture, so that sending the log can't feed itself.
    if (xPortInIsrContext() != 0 || xTaskGetCurrentTaskHandle() == task_.handle()) return;
    const std::unique_lock lock{line_lock_, std::try_to_lock};
    if (!lock) return;

    const int written = std::vsnprintf(line_.data(), line_.size(), format, args);
    if (written <= 0) return;
    std::size_t size = std::min(static_cast<std::size_t>(written), line_.size() - 1);
    while (size > 0 && (line_[size - 1] == '\n' || line_[size - 1] == '\r')) --size;
    if (size > 0) (void)xRingbufferSend(ring_, line_.data(), size, 0);  // dropped if full
}

void RemoteDebug::loop() {
    load_crash();
    if (has_crash_.load()) hooks_.report();

    bool reported_on = false;
    int64_t next_report_us = REPORT_PERIOD_US;
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, LOOP_PERIOD);
        update_logs(reported_on);
        send_logs();
        if (dump_requested_.exchange(false)) send_dump();
        if (erase_requested_.exchange(false)) erase_dump();

        const int64_t now_us = esp_timer_get_time();
        if (now_us >= next_report_us) {
            next_report_us = now_us + REPORT_PERIOD_US;
            hooks_.report();
        }
    }
}

void RemoteDebug::load_crash() {
    esp_core_dump_summary_t summary{};
    if (esp_core_dump_get_summary(&summary) != ESP_OK) return;

    copy_safe(crash_.task, summary.exc_task, sizeof summary.exc_task);
    copy_safe(crash_.elf, reinterpret_cast<const char*>(summary.app_elf_sha256),
              sizeof summary.app_elf_sha256);
    crash_.pc = summary.exc_pc;
    crash_.ra = summary.ex_info.ra;
    crash_.cause = summary.ex_info.mcause;
    crash_.value = summary.ex_info.mtval;
    has_crash_.store(true);
    ESP_LOGW(TAG, "A crash is saved: task %s, pc 0x%08lx", crash_.task.data(),
             static_cast<unsigned long>(crash_.pc));
}

void RemoteDebug::update_logs(bool& reported_on) {
    if (logs_on_.load() && esp_timer_get_time() - logs_since_us_.load() >= LOG_SESSION_US) {
        ESP_LOGI(TAG, "Logs off after an hour");
        logs_on_.store(false);
    }
    if (logs_on_.load() != reported_on) {
        reported_on = logs_on_.load();
        hooks_.report();
    }
}

void RemoteDebug::send_logs() {
    const bool on = logs_on_.load();
    // While disconnected the buffer holds on to the first lines, which show what went wrong.
    while (!on || hooks_.connected()) {
        std::size_t size = 0;
        void* item = xRingbufferReceive(ring_, &size, 0);
        if (item == nullptr) return;
        if (on) (void)hooks_.send_log({static_cast<const char*>(item), size});
        vRingbufferReturnItem(ring_, item);
    }
}

void RemoteDebug::send_dump() {
    std::size_t address = 0;
    std::size_t total = 0;
    if (esp_core_dump_image_get(&address, &total) != ESP_OK) {
        // A chunk with a total of 0 tells the reader there's nothing to wait for.
        total = 0;
    }
    ESP_LOGI(TAG, "Sending core dump: %zu B", total);

    std::size_t offset = 0;
    do {
        const std::size_t size = std::min(total - offset, DUMP_CHUNK_BYTES);
        put_u32(chunk_.data(), static_cast<uint32_t>(offset));
        put_u32(chunk_.data() + 4, static_cast<uint32_t>(total));
        if (size > 0) {
            const esp_err_t err = esp_flash_read(esp_flash_default_chip, chunk_.data() + DUMP_HEADER_BYTES,
                                                 address + offset, size);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Can't read the core dump: %s", esp_err_to_name(err));
                return;
            }
        }
        if (!hooks_.send_dump({chunk_.data(), DUMP_HEADER_BYTES + size})) {
            ESP_LOGW(TAG, "Core dump not sent: disconnected");  // the reader sees a gap, and asks again
            return;
        }
        offset += size;
        vTaskDelay(DUMP_CHUNK_GAP);
    } while (offset < total);
}

void RemoteDebug::erase_dump() {
    const esp_err_t err = esp_core_dump_image_erase();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Can't erase the core dump: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "Core dump erased");
    has_crash_.store(false);
    hooks_.report();
}
