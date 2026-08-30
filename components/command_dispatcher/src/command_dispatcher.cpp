#include "shahbaz/command/command_dispatcher.hpp"

#include <cstddef>
#include <cstdint>

namespace shahbaz::command {
namespace {

using protocol::MessageType;
using protocol::NackReason;

constexpr std::size_t kEmptyPayloadLength = 0U;
constexpr std::size_t kTokenPayloadLength = 8U;
constexpr std::size_t kSetSensorRatePayloadLength = 6U;
constexpr std::size_t kDirectPulsePayloadLength = 3U;   // <BH>
constexpr std::size_t kActuatorPayloadLength = 4U;      // <BBH>
constexpr std::size_t kControlModePayloadLength = 1U;
constexpr std::uint8_t kCurrentSensorInstance = 0U;

struct EnvelopeResult final {
    bool valid{false};
    NackReason nack_reason{NackReason::MalformedMessage};
    ValidationError validation_error{ValidationError::InvalidEnvelope};
};

[[nodiscard]] auto readLe16(const std::uint8_t* input) noexcept -> std::uint16_t {
    return static_cast<std::uint16_t>(input[0]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(input[1]) << 8U);
}

[[nodiscard]] auto readLe32(const std::uint8_t* input) noexcept -> std::uint32_t {
    std::uint32_t value = 0U;
    for (std::size_t i = 0U; i < 4U; ++i) {
        value |= static_cast<std::uint32_t>(input[i]) << (8U * i);
    }
    return value;
}

[[nodiscard]] auto isSafetyOverride(const MessageType type) noexcept -> bool {
    return type == MessageType::EmergencyStop || type == MessageType::Disarm;
}

[[nodiscard]] auto requiresSynchronizedSenderTime(const MessageType type) noexcept -> bool {
    switch (type) {
        case MessageType::Heartbeat:
        case MessageType::HeartbeatAck:
        case MessageType::StartTelemetry:
        case MessageType::StopTelemetry:
        case MessageType::SetSensorRate:
        case MessageType::ArmRequest:
        case MessageType::ArmConfirm:
        case MessageType::ActuatorCommand:
        case MessageType::MotorCommand:
        case MessageType::ServoCommand:
        case MessageType::SetControlMode:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] auto isDeviceToHostOnly(const MessageType type) noexcept -> bool {
    switch (type) {
        case MessageType::DeviceInfoResponse:
        case MessageType::SensorSample:
        case MessageType::DeviceStatusResponse:
        case MessageType::TimeSyncResponse:
        case MessageType::CommandAck:
        case MessageType::CommandNack:
        case MessageType::ProtocolError:
        case MessageType::SafetyState:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] auto isAllowedInSafetyState(const MessageType type,
                                          const safety::SafetyState state) noexcept -> bool {
    switch (state) {
        case safety::SafetyState::Disarmed:
            switch (type) {
                case MessageType::ActuatorCommand:
                case MessageType::MotorCommand:
                case MessageType::ServoCommand:
                    return false;
                default:
                    return true;
            }
        case safety::SafetyState::Armed:
            switch (type) {
                case MessageType::DeviceInfoRequest:
                case MessageType::StartTelemetry:
                case MessageType::StopTelemetry:
                case MessageType::SetSensorRate:
                case MessageType::DeviceStatusRequest:
                case MessageType::Heartbeat:
                case MessageType::HeartbeatAck:
                case MessageType::Ping:
                case MessageType::Pong:
                case MessageType::TimeSyncRequest:
                case MessageType::ActuatorCommand:
                case MessageType::MotorCommand:
                case MessageType::ServoCommand:
                case MessageType::SetControlMode:
                    return true;
                default:
                    return false;
            }
        case safety::SafetyState::Booting:
        case safety::SafetyState::Arming:
        case safety::SafetyState::Failsafe:
        case safety::SafetyState::Fault:
        case safety::SafetyState::EmergencyStopped:
            switch (type) {
                case MessageType::DeviceInfoRequest:
                case MessageType::StopTelemetry:
                case MessageType::DeviceStatusRequest:
                case MessageType::Heartbeat:
                case MessageType::HeartbeatAck:
                case MessageType::Ping:
                case MessageType::Pong:
                case MessageType::TimeSyncRequest:
                    return true;
                default:
                    return false;
            }
    }
    return false;
}

[[nodiscard]] auto validateEnvelope(const protocol::DecodedFrame& frame) noexcept -> EnvelopeResult {
    if (frame.header.version != protocol::kProtocolVersion) {
        return {false, NackReason::UnsupportedVersion, ValidationError::InvalidEnvelope};
    }
    if (frame.header.header_length != protocol::kHeaderLength ||
        frame.header.reserved != 0U || frame.header.flags != 0U) {
        return {false, NackReason::MalformedMessage, ValidationError::InvalidEnvelope};
    }
    if (!protocol::isKnownMessageType(static_cast<std::uint16_t>(frame.header.message_type))) {
        return {false, NackReason::UnknownMessageType, ValidationError::InvalidEnvelope};
    }
    if (frame.payload_size > protocol::kMaxPayloadLength ||
        frame.header.payload_length != frame.payload_size) {
        return {false, NackReason::InvalidLength, ValidationError::InvalidEnvelope};
    }
    return {true, NackReason::MalformedMessage, ValidationError::None};
}

[[nodiscard]] auto makeNack(const protocol::DecodedFrame& frame,
                            const NackReason reason,
                            const ValidationError error,
                            const bool frame_updated = false) noexcept -> DispatchResult {
    DispatchResult result{};
    result.accepted = false;
    result.reply = ReplyKind::CommandNack;
    result.nack_reason = reason;
    result.validation_error = error;
    result.request_sequence = frame.header.sequence;
    result.frame_freshness_updated = frame_updated;
    return result;
}

[[nodiscard]] auto makeAccepted(const protocol::DecodedFrame& frame,
                                const ReplyKind reply,
                                const ApplicationAction action,
                                const bool frame_updated) noexcept -> DispatchResult {
    DispatchResult result{};
    result.accepted = true;
    result.reply = reply;
    result.validation_error = ValidationError::None;
    result.action = action;
    result.request_sequence = frame.header.sequence;
    result.frame_freshness_updated = frame_updated;
    return result;
}

[[nodiscard]] auto exactLength(const protocol::DecodedFrame& frame,
                               const std::size_t expected) noexcept -> bool {
    return frame.payload_size == expected;
}

[[nodiscard]] auto mapSafetyReject(const protocol::DecodedFrame& frame,
                                   const safety::SafetyCommandResult safety_result,
                                   const bool frame_updated) noexcept -> DispatchResult {
    switch (safety_result) {
        case safety::SafetyCommandResult::RejectedActuatorUnavailable:
            return makeNack(frame, NackReason::InvalidState,
                            ValidationError::ActuatorUnavailable, frame_updated);
        case safety::SafetyCommandResult::RejectedActuatorFailure:
            return makeNack(frame, NackReason::InvalidState,
                            ValidationError::ActuatorRejected, frame_updated);
        case safety::SafetyCommandResult::RejectedLinkUnhealthy:
        case safety::SafetyCommandResult::RejectedInvalidState:
            return makeNack(frame, NackReason::InvalidState,
                            ValidationError::InvalidSafetyState, frame_updated);
        case safety::SafetyCommandResult::AcceptedSafeState:
        case safety::SafetyCommandResult::AcceptedArmed:
        case safety::SafetyCommandResult::AcceptedActuatorCommand:
            break;
    }
    return makeNack(frame, NackReason::InvalidState,
                    ValidationError::ActuatorRejected, frame_updated);
}

}  // namespace

CommandDispatcher::CommandDispatcher(safety::ISafetySupervisor& safety_supervisor,
                                     DispatcherConfig config) noexcept
    : safety_supervisor_(safety_supervisor), config_(config) {}

auto CommandDispatcher::dispatch(const protocol::DecodedFrame& frame,
                                 const DispatchContext& context) noexcept -> DispatchResult {
    const MessageType type = frame.header.message_type;
    const auto envelope = validateEnvelope(frame);

    if (isSafetyOverride(type)) {
        if (type == MessageType::EmergencyStop) {
            (void)safety_supervisor_.handleEmergencyStop(context.dispatch_monotonic_us);
        } else {
            (void)safety_supervisor_.handleDisarm(context.dispatch_monotonic_us);
        }
        const bool local_timing_valid =
            context.dispatch_monotonic_us >= context.received_monotonic_us &&
            (context.dispatch_monotonic_us - context.received_monotonic_us) <=
                config_.maximum_local_dispatch_age_us;
        const bool canonical = envelope.valid && exactLength(frame, kEmptyPayloadLength) &&
                               local_timing_valid &&
                               context.sender_freshness != SenderFreshness::StaleOrExpired &&
                               classifySequence(frame.header.sequence) == SequenceStatus::Accept;
        bool frame_updated = false;
        bool control_updated = false;
        if (canonical) {
            frame_updated = safety_supervisor_.observeValidFrame(
                context.received_monotonic_us, context.dispatch_monotonic_us);
            if (frame_updated) {
                control_updated = safety_supervisor_.observeValidControlCommand(
                    context.received_monotonic_us, context.dispatch_monotonic_us);
            }
            if (control_updated) {
                commitSequence(frame.header.sequence);
            }
        }
        auto result = makeAccepted(
            frame, ReplyKind::CommandAck,
            type == MessageType::EmergencyStop ? ApplicationAction::EmergencyStopApplied
                                               : ApplicationAction::DisarmApplied,
            frame_updated);
        result.control_freshness_updated = control_updated;
        result.noncanonical_safe_payload_ignored = !canonical;
        return result;
    }

    if (!envelope.valid) {
        return makeNack(frame, envelope.nack_reason, envelope.validation_error);
    }
    if (context.dispatch_monotonic_us < context.received_monotonic_us) {
        return makeNack(frame, NackReason::StaleOrExpired,
                        ValidationError::LocalDispatchExpired);
    }

    if (context.sender_freshness == SenderFreshness::StaleOrExpired) {
        return makeNack(frame, NackReason::StaleOrExpired,
                        ValidationError::SenderStaleOrExpired);
    }
    if (context.sender_freshness == SenderFreshness::NotSynchronized &&
        requiresSynchronizedSenderTime(type)) {
        return makeNack(frame, NackReason::StaleOrExpired,
                        ValidationError::SenderTimeNotSynchronized);
    }

    const bool frame_updated = safety_supervisor_.observeValidFrame(
        context.received_monotonic_us, context.dispatch_monotonic_us);
    if (!frame_updated) {
        return makeNack(frame, NackReason::InvalidState,
                        ValidationError::SupervisorRejectedTimestamp);
    }
    if (isDeviceToHostOnly(type)) {
        return makeNack(frame, NackReason::InvalidState,
                        ValidationError::UnsupportedDirection, true);
    }

    switch (type) {
        case MessageType::DeviceInfoRequest:
        case MessageType::StartTelemetry:
        case MessageType::StopTelemetry:
        case MessageType::DeviceStatusRequest:
        case MessageType::Heartbeat:
        case MessageType::HeartbeatAck:
        case MessageType::ArmRequest:
        case MessageType::ArmConfirm:
            if (!exactLength(frame, kEmptyPayloadLength)) {
                return makeNack(frame, NackReason::InvalidLength,
                                ValidationError::InvalidPayloadLength, true);
            }
            break;
        case MessageType::Ping:
        case MessageType::Pong:
        case MessageType::TimeSyncRequest:
            if (!exactLength(frame, kTokenPayloadLength)) {
                return makeNack(frame, NackReason::InvalidLength,
                                ValidationError::InvalidPayloadLength, true);
            }
            break;
        case MessageType::SetSensorRate:
            if (!exactLength(frame, kSetSensorRatePayloadLength)) {
                return makeNack(frame, NackReason::InvalidLength,
                                ValidationError::InvalidPayloadLength, true);
            }
            break;
        case MessageType::MotorCommand:
        case MessageType::ServoCommand:
            if (!exactLength(frame, kDirectPulsePayloadLength)) {
                return makeNack(frame, NackReason::InvalidLength,
                                ValidationError::InvalidPayloadLength, true);
            }
            break;
        case MessageType::ActuatorCommand:
            if (!exactLength(frame, kActuatorPayloadLength)) {
                return makeNack(frame, NackReason::InvalidLength,
                                ValidationError::InvalidPayloadLength, true);
            }
            break;
        case MessageType::SetControlMode:
            if (!exactLength(frame, kControlModePayloadLength)) {
                return makeNack(frame, NackReason::InvalidLength,
                                ValidationError::InvalidPayloadLength, true);
            }
            break;
        default:
            return makeNack(frame, NackReason::InvalidState,
                            ValidationError::UnsupportedDirection, true);
    }

    if (!isAllowedInSafetyState(type, safety_supervisor_.state())) {
        safety_supervisor_.reportInvalidCommand();
        return makeNack(frame, NackReason::InvalidState,
                        ValidationError::InvalidSafetyState, true);
    }
    if ((context.dispatch_monotonic_us - context.received_monotonic_us) >
        config_.maximum_local_dispatch_age_us) {
        return makeNack(frame, NackReason::StaleOrExpired,
                        ValidationError::LocalDispatchExpired, true);
    }
    const auto sequence_status = classifySequence(frame.header.sequence);
    if (sequence_status == SequenceStatus::Duplicate) {
        return makeNack(frame, NackReason::DuplicateSequence,
                        ValidationError::DuplicateSequence, true);
    }
    if (sequence_status == SequenceStatus::OutOfOrder) {
        return makeNack(frame, NackReason::OutOfOrderSequence,
                        ValidationError::OutOfOrderSequence, true);
    }

    DispatchResult result{};
    switch (type) {
        case MessageType::DeviceInfoRequest:
            result = makeAccepted(frame, ReplyKind::DeviceInfoResponse,
                                  ApplicationAction::RequestDeviceInfo, true);
            break;
        case MessageType::StartTelemetry:
            result = makeAccepted(frame, ReplyKind::CommandAck,
                                  ApplicationAction::StartTelemetry, true);
            break;
        case MessageType::StopTelemetry:
            result = makeAccepted(frame, ReplyKind::CommandAck,
                                  ApplicationAction::StopTelemetry, true);
            break;
        case MessageType::DeviceStatusRequest:
            result = makeAccepted(frame, ReplyKind::DeviceStatusResponse,
                                  ApplicationAction::RequestDeviceStatus, true);
            break;
        case MessageType::Heartbeat:
            result = makeAccepted(frame, ReplyKind::HeartbeatAck,
                                  ApplicationAction::HeartbeatReceived, true);
            break;
        case MessageType::HeartbeatAck:
            result = makeAccepted(frame, ReplyKind::None,
                                  ApplicationAction::HeartbeatAckReceived, true);
            break;
        case MessageType::Ping:
            result = makeAccepted(frame, ReplyKind::Pong,
                                  ApplicationAction::PingReceived, true);
            break;
        case MessageType::Pong:
            result = makeAccepted(frame, ReplyKind::None,
                                  ApplicationAction::PongReceived, true);
            break;
        case MessageType::TimeSyncRequest:
            result = makeAccepted(frame, ReplyKind::TimeSyncResponse,
                                  ApplicationAction::TimeSyncRequestReceived, true);
            break;
        case MessageType::SetSensorRate: {
            const auto raw_sensor_id = frame.payload[0];
            const auto instance_id = frame.payload[1];
            const auto interval_us = readLe32(frame.payload.data() + 2U);
            const bool known = raw_sensor_id == static_cast<std::uint8_t>(SensorId::Sht3x) ||
                               raw_sensor_id == static_cast<std::uint8_t>(SensorId::Ms5611);
            if (!known || instance_id != kCurrentSensorInstance) {
                return makeNack(frame, NackReason::MalformedMessage,
                                ValidationError::InvalidPayloadValue, true);
            }
            const auto sensor_id = static_cast<SensorId>(raw_sensor_id);
            const auto& bounds = sensor_id == SensorId::Sht3x
                                     ? config_.sht3x_interval
                                     : config_.ms5611_interval;
            if (!bounds.contains(interval_us)) {
                return makeNack(frame, NackReason::MalformedMessage,
                                ValidationError::InvalidPayloadValue, true);
            }
            result = makeAccepted(frame, ReplyKind::CommandAck,
                                  ApplicationAction::SetSensorRate, true);
            result.sensor_rate = {sensor_id, instance_id, interval_us, true};
            break;
        }
        case MessageType::ArmRequest:
        case MessageType::ArmConfirm: {
            const auto sr = safety_supervisor_.handleArmRequest(context.dispatch_monotonic_us);
            if (sr != safety::SafetyCommandResult::AcceptedArmed) {
                return mapSafetyReject(frame, sr, true);
            }
            result = makeAccepted(frame, ReplyKind::CommandAck,
                                  ApplicationAction::ArmApplied, true);
            break;
        }
        case MessageType::MotorCommand:
        case MessageType::ServoCommand:
        case MessageType::ActuatorCommand: {
            safety::ActuatorKind kind{};
            std::uint8_t channel = 0U;
            std::uint16_t pulse_us = 0U;
            if (type == MessageType::ActuatorCommand) {
                if (frame.payload[0] != static_cast<std::uint8_t>(safety::ActuatorKind::Motor) &&
                    frame.payload[0] != static_cast<std::uint8_t>(safety::ActuatorKind::Servo)) {
                    return makeNack(frame, NackReason::MalformedMessage,
                                    ValidationError::InvalidPayloadValue, true);
                }
                kind = static_cast<safety::ActuatorKind>(frame.payload[0]);
                channel = frame.payload[1];
                pulse_us = readLe16(frame.payload.data() + 2U);
            } else {
                kind = type == MessageType::MotorCommand
                           ? safety::ActuatorKind::Motor
                           : safety::ActuatorKind::Servo;
                channel = frame.payload[0];
                pulse_us = readLe16(frame.payload.data() + 1U);
            }
            const bool is_motor = kind == safety::ActuatorKind::Motor;
            const auto channel_limit = is_motor ? config_.motor_channels : config_.servo_channels;
            const auto& pulse_bounds = is_motor ? config_.motor_pulse : config_.servo_pulse;
            if (channel >= channel_limit || !pulse_bounds.contains(pulse_us)) {
                return makeNack(frame, NackReason::MalformedMessage,
                                ValidationError::InvalidPayloadValue, true);
            }
            const auto sr = safety_supervisor_.handleActuatorCommand(context.dispatch_monotonic_us);
            if (sr != safety::SafetyCommandResult::AcceptedActuatorCommand) {
                return mapSafetyReject(frame, sr, true);
            }
            const auto action = type == MessageType::MotorCommand
                                    ? ApplicationAction::MotorCommand
                                    : (type == MessageType::ServoCommand
                                           ? ApplicationAction::ServoCommand
                                           : ApplicationAction::ActuatorCommand);
            result = makeAccepted(frame, ReplyKind::CommandAck, action, true);
            result.actuator = {kind, channel, pulse_us, true};
            break;
        }
        case MessageType::SetControlMode:
            if (frame.payload[0] != 0U) {
                return makeNack(frame, NackReason::MalformedMessage,
                                ValidationError::InvalidPayloadValue, true);
            }
            result = makeAccepted(frame, ReplyKind::CommandAck,
                                  ApplicationAction::SetControlMode, true);
            result.control_mode = {frame.payload[0], true};
            break;
        default:
            return makeNack(frame, NackReason::InvalidState,
                            ValidationError::UnsupportedDirection, true);
    }

    if (type == MessageType::Heartbeat || type == MessageType::HeartbeatAck) {
        if (!safety_supervisor_.observeValidHeartbeat(context.received_monotonic_us,
                                                      context.dispatch_monotonic_us)) {
            return makeNack(frame, NackReason::InvalidState,
                            ValidationError::SupervisorRejectedTimestamp, true);
        }
        result.heartbeat_freshness_updated = true;
    }
    if (type == MessageType::ArmRequest || type == MessageType::ArmConfirm ||
        type == MessageType::ActuatorCommand || type == MessageType::MotorCommand ||
        type == MessageType::ServoCommand) {
        if (!safety_supervisor_.observeValidControlCommand(context.received_monotonic_us,
                                                           context.dispatch_monotonic_us)) {
            return makeNack(frame, NackReason::InvalidState,
                            ValidationError::SupervisorRejectedTimestamp, true);
        }
        result.control_freshness_updated = true;
    }

    commitSequence(frame.header.sequence);
    return result;
}

void CommandDispatcher::resetSessionSequence() noexcept {
    last_sequence_ = 0U;
    sequence_seen_ = false;
}

auto CommandDispatcher::classifySequence(const std::uint32_t sequence) const noexcept -> SequenceStatus {
    if (!sequence_seen_) {
        return SequenceStatus::Accept;
    }
    const auto forward_distance = sequence - last_sequence_;
    if (forward_distance == 0U) {
        return SequenceStatus::Duplicate;
    }
    return forward_distance < UINT32_C(0x80000000)
               ? SequenceStatus::Accept
               : SequenceStatus::OutOfOrder;
}

void CommandDispatcher::commitSequence(const std::uint32_t sequence) noexcept {
    last_sequence_ = sequence;
    sequence_seen_ = true;
}

}  // namespace shahbaz::command
