#pragma once
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <optional>
#include <string_view>
#include "BulbProtocol.hpp"
#include "Callback.hpp"
#include "Mutex.hpp"
#include "StaticTask.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"

/**
 * Diagnoses a bulb that has no cable: sends its log over the network on request,
 * reports the crash saved in flash and sends the whole core dump when asked.
 * Also drives the periodic health report.
 */
class RemoteDebug {
public:
    /** All run in this class's own low-priority task, which may block in them. */
    struct Hooks {
        Callback<bool()> connected;
        Callback<bool(std::string_view line)> send_log;
        Callback<bool(std::string_view chunk)> send_dump;
        Callback<void()> report;  ///< Time for a health report, or logs_on() or crash() changed.
    };

    RemoteDebug() = default;
    RemoteDebug(const RemoteDebug&) = delete;
    RemoteDebug& operator=(const RemoteDebug&) = delete;

    /** Hook the log output and start the task. Only one instance can be started. */
    void start(Hooks hooks);

    /** Send the log over the network or stop. Logging turns itself off after an hour. */
    void set_logs(bool on);
    /** Send the saved core dump. It stays in flash until request_erase(). */
    void request_dump();
    void request_erase();

    [[nodiscard]] bool logs_on() const { return logs_on_.load(); }
    /** The crash saved in flash, if there is one. */
    [[nodiscard]] std::optional<CrashSummary> crash() const;
    [[nodiscard]] std::size_t stack_free_bytes() const { return task_.stack_free_bytes(); }

private:
    static constexpr const char* TAG = "remote_debug";
    static constexpr std::size_t TASK_STACK_BYTES = 6144;  // a TLS write runs on it
    static constexpr UBaseType_t TASK_PRIORITY = 1;
    static constexpr std::size_t LOG_BUFFER_BYTES = 4096;
    static constexpr std::size_t LOG_LINE_BYTES = 200;
    static constexpr std::size_t DUMP_HEADER_BYTES = 8;
    static constexpr std::size_t DUMP_CHUNK_BYTES = 1024;

    static int log_hook(const char* format, va_list args);
    void capture(const char* format, va_list args);

    [[noreturn]] void loop();
    void load_crash();
    void update_logs(bool& reported_on);
    void send_logs();
    void send_dump();
    void erase_dump();

    inline static RemoteDebug* instance_ = nullptr;  // the log hook is a plain function

    Hooks hooks_;
    vprintf_like_t original_log_ = nullptr;
    std::atomic<bool> logs_on_{false};
    std::atomic<int64_t> logs_since_us_{0};
    std::atomic<bool> dump_requested_{false};
    std::atomic<bool> erase_requested_{false};
    std::atomic<bool> has_crash_{false};
    CrashSummary crash_{};  // written before has_crash_ is set, and never again

    rtos::Mutex line_lock_;  // a line that finds this taken is dropped, never waited for
    std::array<char, LOG_LINE_BYTES> line_{};
    StaticRingbuffer_t ring_control_{};
    std::array<uint8_t, LOG_BUFFER_BYTES> ring_storage_{};
    RingbufHandle_t ring_ = nullptr;
    std::array<char, DUMP_HEADER_BYTES + DUMP_CHUNK_BYTES> chunk_{};
    rtos::StaticTask<TASK_STACK_BYTES> task_;
};
