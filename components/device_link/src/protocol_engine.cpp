#include "shahbaz/link/protocol_engine.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace shahbaz::link {
namespace {

void put16(std::uint8_t* out, const std::uint16_t value) noexcept {
    out[0] = static_cast<std::uint8_t>(value & 0xFFU);
    out[1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}
void put32(std::uint8_t* out, const std::uint32_t value) noexcept {
    for (std::size_t i = 0U; i < 4U; ++i) {
        out[i] = static_cast<std::uint8_t>((value >> (8U * i)) & 0xFFU);
    }
}
void put64(std::uint8_t* out, const std::uint64_t value) noexcept {
    for (std::size_t i = 0U; i < 8U; ++i) {
        out[i] = static_cast<std::uint8_t>((value >> (8U * i)) & 0xFFU);
    }
}
std::uint64_t get64(const std::uint8_t* in) noexcept {
    std::uint64_t value = 0U;
    for (std::size_t i = 0U; i < 8U; ++i) {
        value |= static_cast<std::uint64_t>(in[i]) << (8U * i);
    }
    return value;
}

}  // namespace

ProtocolEngine::ProtocolEngine(command::CommandDispatcher& dispatcher,
                               safety::SafetySupervisor& safety,
                               safety::IActuatorController& actuator,
                               sensors::scheduler::SharedSensorScheduler& sensors,
                               SensorTelemetryPublisher& telemetry,
                               DeviceFrameSender& sender,
                               interfaces::IMonotonicClock& clock,
                               DeviceRuntimeInfo runtime_info,
                               ProtocolEngineConfig config) noexcept
    : dispatcher_(dispatcher), safety_(safety), actuator_(actuator), sensors_(sensors),
      telemetry_(telemetry), sender_(sender), clock_(clock), runtime_info_(runtime_info),
      config_(config) {
    if (config_.maximum_sender_age_us == 0U) config_.maximum_sender_age_us = 1U;
}

void ProtocolEngine::increment(std::uint32_t& value) noexcept {
    if (value != std::numeric_limits<std::uint32_t>::max()) ++value;
}

bool ProtocolEngine::sessionBound(const protocol::MessageType type) noexcept {
    switch (type) {
        case protocol::MessageType::StartTelemetry:
        case protocol::MessageType::StopTelemetry:
        case protocol::MessageType::SetSensorRate:
        case protocol::MessageType::Heartbeat:
        case protocol::MessageType::HeartbeatAck:
        case protocol::MessageType::ArmRequest:
        case protocol::MessageType::ArmConfirm:
        case protocol::MessageType::ActuatorCommand:
        case protocol::MessageType::MotorCommand:
        case protocol::MessageType::ServoCommand:
        case protocol::MessageType::SetControlMode:
        case protocol::MessageType::MotorFrameCommand:
            return true;
        default:
            return false;
    }
}

void ProtocolEngine::setConnected(const bool connected,
                                  const std::uint64_t session_token) noexcept {
    const auto now = clock_.now_us();
    connected_ = connected && session_token != 0U;
    session_token_ = connected_ ? session_token : 0U;
    accumulator_.reset();
    dispatcher_.resetSessionSequence();
    time_synchronized_ = false;
    sync_sender_us_ = 0U;
    sync_device_rx_us_ = 0U;
    if (!connected_) telemetry_.setEnabled(false);
    (void)safety_.setUsbConnected(connected_, now);
}

void ProtocolEngine::consume(const interfaces::ConstByteView bytes) noexcept {
    if (!connected_ || !interfaces::isValid(bytes)) return;
    const auto received_us = clock_.now_us();
    (void)safety_.observeUsbTraffic(received_us);
    for (std::size_t i = 0U; i < bytes.size; ++i) {
        increment(statistics_.bytes_received);
        const auto event = accumulator_.pushByte(bytes.data[i], decoded_frame_);
        if (event.event == protocol::StreamEvent::FrameReady) {
            increment(statistics_.frames_received);
            processFrame(decoded_frame_, received_us);
        } else if (event.event == protocol::StreamEvent::FrameRejected ||
                   event.event == protocol::StreamEvent::OversizeDiscarded) {
            increment(statistics_.frames_rejected);
            if (event.status == protocol::FrameStatus::CrcMismatch) safety_.reportCrcError();
            else safety_.reportParserError();
        }
    }
}

command::SenderFreshness ProtocolEngine::senderFreshness(
    const protocol::DecodedFrame& frame,
    const std::uint64_t received_us) const noexcept {
    if (!time_synchronized_) return command::SenderFreshness::NotSynchronized;
    if (frame.header.sender_monotonic_us < sync_sender_us_) {
        return command::SenderFreshness::StaleOrExpired;
    }

    const auto sender_delta = frame.header.sender_monotonic_us - sync_sender_us_;
    if (sender_delta > std::numeric_limits<std::uint64_t>::max() - sync_device_rx_us_) {
        return command::SenderFreshness::StaleOrExpired;
    }
    const auto expected_device_rx_us = sync_device_rx_us_ + sender_delta;

    if (expected_device_rx_us > received_us) {
        const auto future_skew = expected_device_rx_us - received_us;
        return future_skew <= config_.maximum_sender_future_skew_us
                   ? command::SenderFreshness::Fresh
                   : command::SenderFreshness::StaleOrExpired;
    }

    return (received_us - expected_device_rx_us) <= config_.maximum_sender_age_us
               ? command::SenderFreshness::Fresh
               : command::SenderFreshness::StaleOrExpired;
}

bool ProtocolEngine::validateAndStripSessionToken(const protocol::DecodedFrame& input,
                                                  protocol::DecodedFrame& output) noexcept {
    output = input;
    if (!sessionBound(input.header.message_type)) return true;
    if (input.payload_size < sizeof(std::uint64_t) || session_token_ == 0U ||
        get64(input.payload.data()) != session_token_) {
        increment(statistics_.session_rejects);
        safety_.reportInvalidCommand();
        sendNack(input.header.sequence, protocol::NackReason::SessionMismatch,
                 command::ValidationError::SessionTokenInvalid);
        return false;
    }

    const auto remaining = input.payload_size - sizeof(std::uint64_t);
    if (remaining != 0U) {
        std::move(input.payload.begin() + static_cast<std::ptrdiff_t>(sizeof(std::uint64_t)),
                  input.payload.begin() + static_cast<std::ptrdiff_t>(input.payload_size),
                  output.payload.begin());
    }
    output.payload_size = remaining;
    output.header.payload_length = static_cast<std::uint16_t>(remaining);
    return true;
}

void ProtocolEngine::establishTimeMapping(const protocol::DecodedFrame& frame,
                                          const std::uint64_t received_us) noexcept {
    sync_sender_us_ = frame.header.sender_monotonic_us;
    sync_device_rx_us_ = received_us;
    time_synchronized_ = true;
}

void ProtocolEngine::processFrame(const protocol::DecodedFrame& frame,
                                  const std::uint64_t received_us) noexcept {
    if (!validateAndStripSessionToken(frame, session_frame_)) return;

    const auto now = clock_.now_us();
    auto freshness = senderFreshness(session_frame_, received_us);
    if (session_frame_.header.message_type == protocol::MessageType::TimeSyncRequest) {
        // TimeSync must be able to repair an aged/drifted mapping. Preserve the
        // monotonic non-regression invariant, but do not judge a new sync
        // request by the very mapping it is intended to replace.
        freshness = (time_synchronized_ && session_frame_.header.sender_monotonic_us < sync_sender_us_)
                        ? command::SenderFreshness::StaleOrExpired
                        : command::SenderFreshness::NotSynchronized;
    }
    const auto result = dispatcher_.dispatch(session_frame_, {received_us, now, freshness});
    if (!result.accepted) {
        if (result.validation_error == command::ValidationError::SenderStaleOrExpired ||
            result.validation_error == command::ValidationError::SenderTimeNotSynchronized) {
            increment(statistics_.freshness_rejects);
        }
        sendReply(result, session_frame_, received_us, now);
        return;
    }

    if (result.action == command::ApplicationAction::TimeSyncRequestReceived) {
        establishTimeMapping(session_frame_, received_us);
    }
    if (!applyAction(result, session_frame_, now)) {
        sendNack(result.request_sequence, protocol::NackReason::InvalidState,
                 command::ValidationError::ActuatorRejected);
        return;
    }
    sendReply(result, session_frame_, received_us, now);
}

auto ProtocolEngine::applyAction(const command::DispatchResult& result,
                                 const protocol::DecodedFrame&,
                                 const std::uint64_t) noexcept -> bool {
    switch (result.action) {
        case command::ApplicationAction::StartTelemetry:
            telemetry_.setEnabled(true);
            return true;
        case command::ApplicationAction::StopTelemetry:
            telemetry_.setEnabled(false);
            return true;
        case command::ApplicationAction::SetSensorRate:
            if (!result.sensor_rate.present) return false;
            switch (result.sensor_rate.sensor_id) {
                case command::SensorId::Sht3x:
                    return sensors_.set_sht3x_interval_us(
                        result.sensor_rate.interval_us);
                case command::SensorId::Ms5611:
                    return sensors_.set_ms5611_interval_us(
                        result.sensor_rate.interval_us);
                case command::SensorId::Vl53l0x:
                    // All four instances share one fair acquisition cadence;
                    // the validated instance identifies the caller's intent
                    // but cannot create asymmetric bus starvation.
                    return sensors_.set_vl53l0x_interval_us(
                        result.sensor_rate.interval_us);
            }
            return false;
        case command::ApplicationAction::MotorCommand:
        case command::ApplicationAction::ServoCommand:
        case command::ApplicationAction::ActuatorCommand:
            if (!result.actuator.present) return false;
            if (actuator_.writePulseUs(result.actuator.kind,
                                       result.actuator.channel,
                                       result.actuator.pulse_us) != safety::ActuatorStatus::Ok) {
                safety_.reportActuatorFailure();
                return false;
            }
            return true;
        case command::ApplicationAction::MotorFrameCommand:
            if (!result.motor_frame.present) return false;
            if (actuator_.writeMotorFrame(result.motor_frame.pulse_us) !=
                safety::ActuatorStatus::Ok) {
                // The controller contract already forces all outputs safe on
                // a frame-application failure. Latch the system fault too so
                // no later command can silently resume a partial generation.
                safety_.reportActuatorFailure();
                return false;
            }
            return true;
        default:
            return true;
    }
}

void ProtocolEngine::countReply(const interfaces::TransportStatus status) noexcept {
    if (status == interfaces::TransportStatus::Accepted) increment(statistics_.replies_sent);
    else increment(statistics_.reply_drops);
}

void ProtocolEngine::sendNack(const std::uint32_t request_sequence,
                              const protocol::NackReason reason,
                              const command::ValidationError validation) noexcept {
    std::array<std::uint8_t, 8U> payload{};
    put32(payload.data(), request_sequence);
    put16(payload.data() + 4U, static_cast<std::uint16_t>(reason));
    put16(payload.data() + 6U, static_cast<std::uint16_t>(validation));
    countReply(sender_.send(protocol::MessageType::CommandNack,
                            protocol::MessagePriority::High,
                            {payload.data(), payload.size()}));
}

void ProtocolEngine::sendReply(const command::DispatchResult& result,
                               const protocol::DecodedFrame& request,
                               const std::uint64_t received_us,
                               const std::uint64_t now_us) noexcept {
    if (result.reply == command::ReplyKind::None) return;
    if (result.reply == command::ReplyKind::CommandNack) {
        sendNack(result.request_sequence, result.nack_reason, result.validation_error);
        return;
    }

    std::array<std::uint8_t, 64U> payload{};
    std::size_t size = 0U;
    protocol::MessageType type = protocol::MessageType::CommandAck;
    auto priority = protocol::MessagePriority::High;
    switch (result.reply) {
        case command::ReplyKind::CommandAck:
            put32(payload.data(), result.request_sequence);
            payload[4] = static_cast<std::uint8_t>(result.action);
            size = 5U;
            type = protocol::MessageType::CommandAck;
            break;
        case command::ReplyKind::HeartbeatAck:
            type = protocol::MessageType::HeartbeatAck;
            size = 0U;
            priority = protocol::MessagePriority::Critical;
            break;
        case command::ReplyKind::Pong:
            type = protocol::MessageType::Pong;
            size = 8U;
            for (std::size_t i = 0U; i < size; ++i) payload[i] = request.payload[i];
            break;
        case command::ReplyKind::TimeSyncResponse: {
            type = protocol::MessageType::TimeSyncResponse;
            const auto client_send = get64(request.payload.data());
            put64(payload.data(), client_send);
            put64(payload.data() + 8U, received_us);
            put64(payload.data() + 16U, now_us);
            put64(payload.data() + 24U, session_token_);
            size = 32U;
            break;
        }
        case command::ReplyKind::DeviceInfoResponse:
            type = protocol::MessageType::DeviceInfoResponse;
            payload[0] = protocol::kProtocolVersion;
            payload[1] = 1U;  // ESP32-S3 target family
            payload[2] = runtime_info_.supported_motor_channels;
            payload[3] = runtime_info_.supported_servo_channels;
            put32(payload.data() + 4U, runtime_info_.detected_flash_bytes);
            put32(payload.data() + 8U, runtime_info_.detected_psram_bytes);
            put32(payload.data() + 12U, runtime_info_.board_issue_mask);
            payload[16] = runtime_info_.active_motor_channels;
            payload[17] = runtime_info_.active_servo_channels;
            payload[18] = runtime_info_.actuator_available ? 1U : 0U;
            payload[19] = runtime_info_.actuators_enabled_by_config ? 1U : 0U;
            size = 20U;
            break;
        case command::ReplyKind::DeviceStatusResponse: {
            type = protocol::MessageType::DeviceStatusResponse;
            payload[0] = static_cast<std::uint8_t>(safety_.state());
            payload[1] = static_cast<std::uint8_t>(safety_.communicationState(now_us));
            payload[2] = telemetry_.enabled() ? 1U : 0U;
            payload[3] = actuator_.armed() ? 1U : 0U;
            payload[4] = sensors_.sht3x().health().online ? 1U : 0U;
            payload[5] = sensors_.ms5611().health().online ? 1U : 0U;
            const auto* const rangefinders = sensors_.vl53l0x_array();
            for (std::size_t instance = 0U;
                 instance < sensors::vl53l0x::kSensorCount;
                 ++instance) {
                const auto lifecycle = rangefinders == nullptr
                    ? sensors::vl53l0x::Lifecycle::DisabledOrAbsent
                    : rangefinders->lifecycle(instance);
                payload[6U + instance] = static_cast<std::uint8_t>(lifecycle);
            }
            size = 10U;
            break;
        }
        case command::ReplyKind::None:
        case command::ReplyKind::CommandNack:
            return;
    }
    countReply(sender_.send(type, priority, {payload.data(), size}));
}

}  // namespace shahbaz::link
