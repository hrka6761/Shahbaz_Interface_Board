#pragma once

#include <cstdint>

namespace shahbaz::safety {

enum class SafetyState : std::uint8_t {
    Booting,
    Disarmed,
    Arming,
    Armed,
    Failsafe,
    Fault,
    EmergencyStopped,
};

enum class CommunicationState : std::uint8_t {
    Disconnected,
    ConnectedInactive,
    TrafficPresentButInvalid,
    HeartbeatMissing,
    Healthy,
};

enum class FaultReason : std::uint8_t {
    None,
    HardwareMismatch,
    SafetyQueueOverflow,
    CriticalTaskFailure,
    WatchdogFailure,
    MonotonicClockRegression,
    ActuatorInvariantViolation,
    ActuatorHardwareFailure,
};

enum class SafetyCommandResult : std::uint8_t {
    AcceptedSafeState,
    AcceptedArmed,
    AcceptedActuatorCommand,
    RejectedInvalidState,
    RejectedLinkUnhealthy,
    RejectedActuatorUnavailable,
    RejectedActuatorFailure,
};

struct FreshnessStamp final {
    std::uint64_t monotonic_us{0U};
    bool observed{false};
};

struct FreshnessTimestamps final {
    FreshnessStamp usb_traffic{};
    FreshnessStamp valid_frame{};
    FreshnessStamp valid_heartbeat{};
    FreshnessStamp valid_control_command{};
};

struct SafetyCounters final {
    std::uint32_t missed_heartbeats{0U};
    std::uint32_t usb_reconnects{0U};
    std::uint32_t crc_errors{0U};
    std::uint32_t parser_errors{0U};
    std::uint32_t invalid_commands{0U};
    std::uint32_t rejected_arm_commands{0U};
    std::uint32_t rejected_actuator_commands{0U};
};

struct SafetyConfig final {
    std::uint64_t heartbeat_timeout_us{1'000'000U};
};

}  // namespace shahbaz::safety
