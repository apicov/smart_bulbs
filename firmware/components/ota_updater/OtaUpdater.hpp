#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <string_view>
#include "Callback.hpp"
#include "OtaPolicy.hpp"
#include "StaticTask.hpp"

/**
 * Installs signed firmware from a fixed URL, only on command, and proves a new
 * image works before keeping it. The bootloader goes back to the old image if
 * this one restarts before it's confirmed.
 */
class OtaUpdater {
public:
    /** All run in the updater's task. */
    struct Hooks {
        Callback<bool()> busy;            ///< A restart now would lose a sleep timer or alarm.
        Callback<bool()> connected;       ///< MQTT is up, which is what proves a new image.
        Callback<void()> before_restart;  ///< Save anything that shouldn't be lost.
        Callback<void()> on_status;       ///< status(), progress() or confirmed() changed.
    };

    OtaUpdater() = default;
    OtaUpdater(const OtaUpdater&) = delete;
    OtaUpdater& operator=(const OtaUpdater&) = delete;

    /** Start the task. `url` must outlive the updater. Everything else needs this called first. */
    void start(const char* url, Hooks hooks);

    /** Check the server and install if there's something new. Returns at once. */
    void request(bool force);

    [[nodiscard]] OtaStatus status() const { return status_.load(); }
    [[nodiscard]] uint8_t progress() const { return progress_.load(); }  ///< Percent of the download.
    [[nodiscard]] bool confirmed() const { return confirmed_.load(); }

    [[nodiscard]] static std::string_view running_version();
    /** The version that failed here, or empty if none has. */
    [[nodiscard]] std::string_view rolled_back_version() const { return rolled_back_.data(); }

    [[nodiscard]] std::size_t stack_free_bytes() const { return task_.stack_free_bytes(); }

private:
    static constexpr const char* TAG = "ota";
    // A TLS handshake runs on this stack.
    static constexpr std::size_t TASK_STACK_BYTES = 8192;
    static constexpr UBaseType_t TASK_PRIORITY = 2;  // below the light and schedule tasks

    const char* url_ = nullptr;
    Hooks hooks_;
    std::array<char, 32> rolled_back_{};  // same size as esp_app_desc_t::version
    std::atomic<OtaStatus> status_{OtaStatus::Idle};
    std::atomic<uint8_t> progress_{0};
    std::atomic<bool> confirmed_{true};
    std::atomic<bool> working_{false};  // a request is queued or running
    std::atomic<bool> force_{false};
    rtos::StaticTask<TASK_STACK_BYTES> task_;

    [[noreturn]] void loop();
    void prove_image();
    void update(bool force);
    /** Install what the server has. True if the bulb should now restart. */
    [[nodiscard]] bool download(bool force);
    void set_status(OtaStatus status);
    void set_progress(uint8_t percent);
};
