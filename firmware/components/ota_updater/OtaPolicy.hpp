#pragma once
#include <chrono>
#include <cstdint>
#include <string_view>

// Update rules, free of ESP-IDF so they can be unit tested on the host.

enum class OtaStatus : uint8_t {
    Idle,
    Checking,
    Downloading,
    Waiting,  ///< Installed, restarting once the sleep timer or alarm has finished.
    Restarting,
    Busy,         ///< Refused: a sleep timer or alarm is running. Send the command again later.
    UpToDate,     ///< The server has the version already running.
    RolledBack,   ///< The server has the version that failed here. Publish a new one, or use force.
    Unconfirmed,  ///< Refused: this image hasn't proved itself yet.
    Rejected,     ///< The image failed validation, such as a missing or wrong signature.
    Failed,       ///< Download or flash error. The running image is untouched.
};

constexpr const char* ota_status_name(OtaStatus status) {
    switch (status) {
    case OtaStatus::Idle:
        return "idle";
    case OtaStatus::Checking:
        return "checking";
    case OtaStatus::Downloading:
        return "downloading";
    case OtaStatus::Waiting:
        return "waiting";
    case OtaStatus::Restarting:
        return "restarting";
    case OtaStatus::Busy:
        return "busy";
    case OtaStatus::UpToDate:
        return "up_to_date";
    case OtaStatus::RolledBack:
        return "rolled_back";
    case OtaStatus::Unconfirmed:
        return "unconfirmed";
    case OtaStatus::Rejected:
        return "rejected";
    case OtaStatus::Failed:
        return "failed";
    }
    return "unknown";
}

enum class OtaDecision : uint8_t { Install, UpToDate, RolledBack };

/**
 * Whether to install the image on the server. Any new version installs, so an
 * older one can be published to go back. `rolled_back` is empty if nothing has
 * been rolled back. Refusing that version stops a reinstall loop, but a rollback
 * can also come from a broker outage, so `force` allows a retry.
 */
constexpr OtaDecision decide_ota(std::string_view offered, std::string_view running,
                                 std::string_view rolled_back, bool force) {
    if (offered == running) return OtaDecision::UpToDate;
    if (!force && offered == rolled_back) return OtaDecision::RolledBack;
    return OtaDecision::Install;
}

inline constexpr std::chrono::seconds CONFIRM_AFTER_CONNECTED{30};
inline constexpr std::chrono::seconds CONFIRM_DEADLINE{300};

enum class ConfirmAction : uint8_t { Wait, Confirm, RollBack };

/** What to do with a new image, given how long MQTT has stayed up and how long since boot. */
constexpr ConfirmAction confirm_action(std::chrono::seconds connected_for, std::chrono::seconds since_boot) {
    if (connected_for >= CONFIRM_AFTER_CONNECTED) return ConfirmAction::Confirm;
    if (since_boot >= CONFIRM_DEADLINE) return ConfirmAction::RollBack;
    return ConfirmAction::Wait;
}
