#pragma once

#include "shahbaz/protocol/wire_protocol.hpp"
#include "shahbaz/safety/actuator_controller.hpp"
#include "shahbaz/safety/safety_supervisor_interface.hpp"

#include <cstdint>

namespace shahbaz::command {

enum class SensorId : std::uint8_t { Sht3x = 1U, Ms5611 = 2U };

struct IntervalBounds final {
    std::uint32_t minimum_us{0U};
    std::uint32_t maximum_us{0U};
    [[nodiscard]] constexpr auto contains(std::uint32_t value) const noexcept -> bool {
        return valid() && value >= minimum_us && value <= maximum_us;
    }
    [[nodiscard]] constexpr auto valid() const noexcept -> bool {
        return minimum_us != 0U && maximum_us >= minimum_us;
    }
};

struct PulseBounds final {
    std::uint16_t minimum_us{900U};
    std::uint16_t maximum_us{2100U};
    [[nodiscard]] constexpr auto contains(std::uint16_t value) const noexcept -> bool {
        return minimum_us != 0U && maximum_us >= minimum_us &&
               value >= minimum_us && value <= maximum_us;
    }
};

struct DispatcherConfig final {
    std::uint64_t maximum_local_dispatch_age_us{100'000U};
    IntervalBounds sht3x_interval{20'000U, 10'000'000U};
    IntervalBounds ms5611_interval{20'000U, 10'000'000U};
    PulseBounds motor_pulse{};
    PulseBounds servo_pulse{500U, 2500U};
    std::uint8_t motor_channels{4U};
    std::uint8_t servo_channels{2U};
};

enum class SenderFreshness : std::uint8_t { Fresh, StaleOrExpired, NotSynchronized };

struct DispatchContext final {
    std::uint64_t received_monotonic_us{0U};
    std::uint64_t dispatch_monotonic_us{0U};
    SenderFreshness sender_freshness{SenderFreshness::NotSynchronized};
};

enum class ApplicationAction : std::uint8_t {
    None,
    RequestDeviceInfo,
    StartTelemetry,
    StopTelemetry,
    SetSensorRate,
    RequestDeviceStatus,
    HeartbeatReceived,
    HeartbeatAckReceived,
    PingReceived,
    PongReceived,
    TimeSyncRequestReceived,
    EmergencyStopApplied,
    DisarmApplied,
    ArmApplied,
    MotorCommand,
    ServoCommand,
    ActuatorCommand,
    SetControlMode,
};

enum class ReplyKind : std::uint8_t {
    None,
    CommandAck,
    CommandNack,
    DeviceInfoResponse,
    DeviceStatusResponse,
    HeartbeatAck,
    Pong,
    TimeSyncResponse,
};

enum class ValidationError : std::uint8_t {
    None,
    InvalidEnvelope,
    InvalidPayloadLength,
    InvalidPayloadValue,
    UnsupportedDirection,
    LocalDispatchExpired,
    SenderStaleOrExpired,
    SenderTimeNotSynchronized,
    SessionTokenInvalid,
    InvalidSafetyState,
    DuplicateSequence,
    OutOfOrderSequence,
    SupervisorRejectedTimestamp,
    ActuatorUnavailable,
    ActuatorRejected,
};

struct SetSensorRateParameters final {
    SensorId sensor_id{SensorId::Sht3x};
    std::uint8_t instance_id{0U};
    std::uint32_t interval_us{0U};
    bool present{false};
};

struct ActuatorParameters final {
    safety::ActuatorKind kind{safety::ActuatorKind::Motor};
    std::uint8_t channel{0U};
    std::uint16_t pulse_us{0U};
    bool present{false};
};

struct ControlModeParameters final {
    std::uint8_t mode{0U};
    bool present{false};
};

struct DispatchResult final {
    bool accepted{false};
    ReplyKind reply{ReplyKind::CommandNack};
    protocol::NackReason nack_reason{protocol::NackReason::MalformedMessage};
    ValidationError validation_error{ValidationError::None};
    ApplicationAction action{ApplicationAction::None};
    std::uint32_t request_sequence{0U};
    bool frame_freshness_updated{false};
    bool heartbeat_freshness_updated{false};
    bool control_freshness_updated{false};
    bool noncanonical_safe_payload_ignored{false};
    SetSensorRateParameters sensor_rate{};
    ActuatorParameters actuator{};
    ControlModeParameters control_mode{};
};

class CommandDispatcher final {
  public:
    CommandDispatcher(safety::ISafetySupervisor& safety_supervisor,
                      DispatcherConfig config) noexcept;
    [[nodiscard]] auto dispatch(const protocol::DecodedFrame& frame,
                                const DispatchContext& context) noexcept
        -> DispatchResult;
    void resetSessionSequence() noexcept;
    [[nodiscard]] auto hasAcceptedSequence() const noexcept -> bool { return sequence_seen_; }
    [[nodiscard]] auto lastAcceptedSequence() const noexcept -> std::uint32_t { return last_sequence_; }

  private:
    enum class SequenceStatus : std::uint8_t { Accept, Duplicate, OutOfOrder };
    [[nodiscard]] auto classifySequence(std::uint32_t sequence) const noexcept -> SequenceStatus;
    void commitSequence(std::uint32_t sequence) noexcept;

    safety::ISafetySupervisor& safety_supervisor_;
    DispatcherConfig config_{};
    std::uint32_t last_sequence_{0U};
    bool sequence_seen_{false};
};

}  // namespace shahbaz::command
