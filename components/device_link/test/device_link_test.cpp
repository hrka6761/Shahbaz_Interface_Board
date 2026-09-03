#include "shahbaz/command/command_dispatcher.hpp"
#include "shahbaz/domain/measurement.hpp"
#include "shahbaz/interfaces/i2c_bus.hpp"
#include "shahbaz/interfaces/monotonic_clock.hpp"
#include "shahbaz/interfaces/telemetry_transport.hpp"
#include "shahbaz/link/device_frame_sender.hpp"
#include "shahbaz/link/protocol_engine.hpp"
#include "shahbaz/link/sensor_telemetry_publisher.hpp"
#include "shahbaz/protocol/wire_protocol.hpp"
#include "shahbaz/safety/safety_supervisor.hpp"
#include "shahbaz/sensors/sensor_scheduler.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::cerr << "CHECK failed line " << __LINE__ << ": " #x << "\n"; return false; } } while (0)

namespace {
using namespace shahbaz;

class FakeClock final : public interfaces::IMonotonicClock {
  public:
    std::uint64_t now{1000U};
    [[nodiscard]] auto now_us() const noexcept -> std::uint64_t override { return now; }
};

class FakeTransport final : public interfaces::ITelemetryTransport {
  public:
    bool is_connected{true};
    struct Sent { std::vector<std::uint8_t> bytes; interfaces::TransportPriority priority{}; std::uint64_t expires{}; };
    std::vector<Sent> sent{};
    [[nodiscard]] auto connected() const noexcept -> bool override { return is_connected; }
    [[nodiscard]] auto send(interfaces::ConstByteView frame, interfaces::TransportPriority p,
                            std::uint64_t expires) noexcept -> interfaces::TransportStatus override {
        if (!is_connected) return interfaces::TransportStatus::Disconnected;
        if (frame.size != 0U && frame.data == nullptr) return interfaces::TransportStatus::InvalidArgument;
        sent.push_back({std::vector<std::uint8_t>(frame.data, frame.data + frame.size), p, expires});
        return interfaces::TransportStatus::Accepted;
    }
};

class FakeI2c final : public interfaces::II2cBus {
  public:
    [[nodiscard]] auto transfer(const interfaces::I2cTransaction&) noexcept -> interfaces::I2cStatus override {
        return interfaces::I2cStatus::NotFound;
    }
    [[nodiscard]] auto recover(std::uint32_t) noexcept -> interfaces::I2cStatus override {
        return interfaces::I2cStatus::Ok;
    }
};

class FakeActuator final : public safety::IActuatorController {
  public:
    bool is_armed{};
    bool enabled{};
    safety::ActuatorKind last_kind{safety::ActuatorKind::Motor};
    std::uint8_t last_channel{};
    std::uint16_t last_pulse{};
    void forceSafe(safety::SafeStopReason reason) noexcept override {
        ++safe_calls;
        last_safe_reason = reason;
        is_armed=false;
        enabled=false;
    }
    [[nodiscard]] auto arm() noexcept -> safety::ActuatorStatus override { is_armed=true; enabled=false; return safety::ActuatorStatus::Ok; }
    [[nodiscard]] auto writePulseUs(safety::ActuatorKind kind, std::uint8_t channel,
                                    std::uint16_t pulse) noexcept -> safety::ActuatorStatus override {
        ++pulse_write_calls;
        if (!is_armed) return safety::ActuatorStatus::NotArmed;
        last_kind=kind; last_channel=channel; last_pulse=pulse; enabled=true; return safety::ActuatorStatus::Ok;
    }
    [[nodiscard]] auto writeMotorFrame(
        const safety::QuadMotorPulseFrame& pulse_us) noexcept
        -> safety::ActuatorStatus override {
        ++motor_frame_calls;
        if (!is_armed) return safety::ActuatorStatus::NotArmed;
        if (motor_frame_result != safety::ActuatorStatus::Ok) {
            forceSafe(safety::SafeStopReason::Fault);
            return motor_frame_result;
        }
        last_motor_frame = pulse_us;
        enabled = true;
        return safety::ActuatorStatus::Ok;
    }
    [[nodiscard]] auto outputsEnabled() const noexcept -> bool override { return enabled; }
    [[nodiscard]] auto armed() const noexcept -> bool override { return is_armed; }
    [[nodiscard]] auto available() const noexcept -> bool override { return true; }
    safety::QuadMotorPulseFrame last_motor_frame{};
    safety::ActuatorStatus motor_frame_result{safety::ActuatorStatus::Ok};
    std::uint32_t motor_frame_calls{};
    std::uint32_t pulse_write_calls{};
    std::uint32_t safe_calls{};
    safety::SafeStopReason last_safe_reason{safety::SafeStopReason::Startup};
};

protocol::DecodedFrame decodeSent(const FakeTransport::Sent& sent) {
    protocol::DecodedFrame out{};
    const auto status = protocol::decodeFrame({sent.bytes.data(), sent.bytes.size()-1U}, out);
    if (status != protocol::FrameStatus::Ok) std::abort();
    return out;
}

std::vector<std::uint8_t> request(protocol::MessageType type, std::uint32_t sequence,
                                  const std::vector<std::uint8_t>& payload = {},
                                  std::uint64_t sender_us = 123U) {
    protocol::EncodedFrame out{};
    auto header = protocol::makeHeader(type, protocol::MessagePriority::High, sequence, sender_us,
                                       static_cast<std::uint16_t>(payload.size()));
    if (protocol::encodeFrame(header, {payload.data(), payload.size()}, out) != protocol::FrameStatus::Ok) std::abort();
    return {out.bytes.begin(), out.bytes.begin() + static_cast<std::ptrdiff_t>(out.size)};
}

void feed(link::ProtocolEngine& engine, const std::vector<std::uint8_t>& bytes) {
    engine.consume({bytes.data(), bytes.size()});
}

std::vector<std::uint8_t> sessionPayload(const std::uint64_t session_token,
                                         const std::vector<std::uint8_t>& payload = {}) {
    std::vector<std::uint8_t> out(8U + payload.size(), 0U);
    for (std::size_t i = 0U; i < 8U; ++i) {
        out[i] = static_cast<std::uint8_t>((session_token >> (8U * i)) & 0xFFU);
    }
    for (std::size_t i = 0U; i < payload.size(); ++i) out[8U + i] = payload[i];
    return out;
}

std::vector<std::uint8_t> motorFramePayload(
    const safety::QuadMotorPulseFrame& pulse_us) {
    std::vector<std::uint8_t> out(13U, 0U);
    out[0] = static_cast<std::uint8_t>(safety::kQuadMotorCount);
    for (std::size_t index = 0U; index < pulse_us.size(); ++index) {
        const auto offset = 1U + index * 3U;
        out[offset] = static_cast<std::uint8_t>(index);
        out[offset + 1U] = static_cast<std::uint8_t>(pulse_us[index] & 0xFFU);
        out[offset + 2U] = static_cast<std::uint8_t>((pulse_us[index] >> 8U) & 0xFFU);
    }
    return out;
}

std::uint64_t read64(const std::uint8_t* input) {
    std::uint64_t value = 0U;
    for (std::size_t i = 0U; i < 8U; ++i) {
        value |= static_cast<std::uint64_t>(input[i]) << (8U * i);
    }
    return value;
}

bool testSenderAndPublisher() {
    FakeClock clock{};
    FakeTransport transport{};
    link::DeviceFrameSender sender{transport, clock};
    const std::array<std::uint8_t,3U> payload{{1U,2U,3U}};
    CHECK(sender.send(protocol::MessageType::Ping, protocol::MessagePriority::Critical,
                      {payload.data(),payload.size()}, 500U) == interfaces::TransportStatus::Accepted);
    CHECK(transport.sent.size() == 1U);
    auto decoded=decodeSent(transport.sent.back());
    CHECK(decoded.header.message_type == protocol::MessageType::Ping);
    CHECK(decoded.header.sequence == 0U);
    CHECK(decoded.payload_size == payload.size());
    CHECK(std::memcmp(decoded.payload.data(), payload.data(), payload.size()) == 0);
    CHECK(transport.sent.back().priority == interfaces::TransportPriority::Critical);
    CHECK(transport.sent.back().expires == 1500U);

    link::SensorTelemetryPublisher publisher{sender};
    domain::SensorSample sample{};
    sample.sensor_id=domain::SensorId::Sht3x; sample.instance_id=0U; sample.sequence=77U;
    sample.monotonic_timestamp_us=999U;
    sample.validity=domain::ValidityFlag::TransportValid | domain::ValidityFlag::CrcValid;
    sample.quality=domain::QualityFlag::Fresh; sample.health_flags=0x12U; sample.field_count=2U;
    sample.fields[0]=domain::make_signed_field(domain::FieldId::AmbientTemperatureMilliCelsius, 23456);
    sample.fields[1]=domain::make_unsigned_field(domain::FieldId::RelativeHumidityMilliPercent, 50123U);
    CHECK(publisher.publish(sample)); // disabled is deliberate no-op success
    CHECK(transport.sent.size() == 1U);
    publisher.setEnabled(true);
    CHECK(publisher.publish(sample));
    CHECK(transport.sent.size() == 2U);
    decoded=decodeSent(transport.sent.back());
    CHECK(decoded.header.message_type == protocol::MessageType::SensorSample);
    CHECK(decoded.payload_size == 39U);
    CHECK(decoded.payload[0] == 1U && decoded.payload[1] == 0U && decoded.payload[26] == 2U);

    sample.fields[0] = {domain::FieldId::AmbientTemperatureMilliCelsius, domain::FieldType::Signed32,
                        static_cast<std::int64_t>(INT32_MAX) + 1};
    CHECK(!publisher.publish(sample));
    CHECK(publisher.statistics().samples_dropped == 1U);
    return true;
}

bool testTimeResyncRepairsAgedMapping() {
    constexpr std::uint64_t kSession = UINT64_C(0x0A0B0C0D0E0F1011);
    FakeClock clock{};
    FakeTransport transport{};
    FakeI2c bus{};
    FakeActuator actuator{};
    safety::SafetySupervisor safety{actuator, safety::SafetyConfig{10'000'000U}};
    CHECK(safety.completeInitialization(clock.now));
    link::DeviceFrameSender sender{transport, clock};
    link::SensorTelemetryPublisher publisher{sender};
    sensors::scheduler::SharedSensorScheduler sensors{bus, clock, publisher};
    command::CommandDispatcher dispatcher{safety, command::DispatcherConfig{}};
    link::ProtocolEngine engine{dispatcher, safety, actuator, sensors, publisher, sender, clock,
                                {}, {250U, 20U}};
    engine.setConnected(true, kSession);

    std::vector<std::uint8_t> sync_payload(8U, 0U);
    sync_payload[0] = 0x11U;
    feed(engine, request(protocol::MessageType::TimeSyncRequest, 1U, sync_payload, 100U));
    CHECK(transport.sent.size() == 1U);
    CHECK(engine.timeSynchronized());
    CHECK(decodeSent(transport.sent.back()).header.message_type ==
          protocol::MessageType::TimeSyncResponse);

    // The old mapping would classify this request as far older than 250 us.
    // Because its sender clock still moves forward, TimeSync is allowed to
    // replace the stale mapping instead of deadlocking clock recovery.
    clock.now = 5'000U;
    sync_payload[0] = 0x22U;
    feed(engine, request(protocol::MessageType::TimeSyncRequest, 2U, sync_payload, 200U));
    CHECK(transport.sent.size() == 2U);
    CHECK(decodeSent(transport.sent.back()).header.message_type ==
          protocol::MessageType::TimeSyncResponse);

    clock.now = 5'010U;
    feed(engine, request(protocol::MessageType::Heartbeat, 3U, sessionPayload(kSession), 210U));
    CHECK(transport.sent.size() == 3U);
    CHECK(decodeSent(transport.sent.back()).header.message_type ==
          protocol::MessageType::HeartbeatAck);

    // A sender-clock regression is still rejected even when the frame type is TimeSync.
    clock.now = 5'020U;
    feed(engine, request(protocol::MessageType::TimeSyncRequest, 4U, sync_payload, 199U));
    CHECK(transport.sent.size() == 4U);
    CHECK(decodeSent(transport.sent.back()).header.message_type ==
          protocol::MessageType::CommandNack);
    return true;
}

bool testProtocolEndToEnd() {
    constexpr std::uint64_t kSession1 = UINT64_C(0x1122334455667788);
    constexpr std::uint64_t kSession2 = UINT64_C(0x8877665544332211);
    FakeClock clock{};
    FakeTransport transport{};
    FakeI2c bus{};
    FakeActuator actuator{};
    safety::SafetySupervisor safety{actuator, safety::SafetyConfig{1'000'000U}};
    CHECK(safety.completeInitialization(clock.now));
    link::DeviceFrameSender sender{transport, clock};
    link::SensorTelemetryPublisher publisher{sender};
    sensors::scheduler::SharedSensorScheduler sensors{bus, clock, publisher};
    command::CommandDispatcher dispatcher{safety, command::DispatcherConfig{}};
    const link::DeviceRuntimeInfo runtime_info{16U, 8U, 0xA5U, 4U, 2U, 4U, 2U, true, true};
    link::ProtocolEngine engine{dispatcher, safety, actuator, sensors, publisher, sender, clock,
                                runtime_info, {250U, 20U}};
    engine.setConnected(true, kSession1);
    CHECK(safety.state() == safety::SafetyState::Disarmed);
    CHECK(engine.sessionToken() == kSession1);

    // Session-bound traffic without the negotiated token is rejected first.
    const safety::QuadMotorPulseFrame first_frame{{1000U, 1100U, 1500U, 1300U}};
    feed(engine, request(protocol::MessageType::MotorFrameCommand, 1U,
                         motorFramePayload(first_frame), clock.now));
    auto reply = decodeSent(transport.sent.back());
    CHECK(reply.header.message_type == protocol::MessageType::CommandNack);
    CHECK(engine.statistics().session_rejects == 1U);

    // A valid session token is still insufficient before time synchronization.
    feed(engine, request(protocol::MessageType::MotorFrameCommand, 1U,
                         sessionPayload(kSession1, motorFramePayload(first_frame)), clock.now));
    reply = decodeSent(transport.sent.back());
    CHECK(reply.header.message_type == protocol::MessageType::CommandNack);

    clock.now += 100U;
    std::vector<std::uint8_t> sync_token(8U, 0U);
    sync_token[0] = 0x5AU;
    feed(engine, request(protocol::MessageType::TimeSyncRequest, 2U, sync_token, clock.now));
    CHECK(engine.timeSynchronized());
    reply = decodeSent(transport.sent.back());
    CHECK(reply.header.message_type == protocol::MessageType::TimeSyncResponse);
    CHECK(reply.payload_size == 32U);
    CHECK(read64(reply.payload.data() + 24U) == kSession1);

    clock.now += 100U;
    feed(engine, request(protocol::MessageType::Heartbeat, 3U,
                         sessionPayload(kSession1), clock.now));
    CHECK(decodeSent(transport.sent.back()).header.message_type == protocol::MessageType::HeartbeatAck);

    clock.now += 100U;
    feed(engine, request(protocol::MessageType::ArmRequest, 4U,
                         sessionPayload(kSession1), clock.now));
    CHECK(safety.state() == safety::SafetyState::Armed);
    CHECK(actuator.armed());
    CHECK(decodeSent(transport.sent.back()).header.message_type == protocol::MessageType::CommandAck);

    clock.now += 100U;
    feed(engine, request(protocol::MessageType::MotorFrameCommand, 5U,
                         sessionPayload(kSession1, motorFramePayload(first_frame)), clock.now));
    CHECK(actuator.motor_frame_calls == 1U);
    CHECK(actuator.pulse_write_calls == 0U);
    CHECK(actuator.last_motor_frame == first_frame);
    CHECK(decodeSent(transport.sent.back()).header.message_type == protocol::MessageType::CommandAck);

    // A sender timestamp outside the configured age window is rejected and
    // cannot update actuator output even with a valid sequence and token.
    const auto frame_before = actuator.last_motor_frame;
    const auto frame_calls_before = actuator.motor_frame_calls;
    const safety::QuadMotorPulseFrame stale_frame{{1600U, 1600U, 1600U, 1600U}};
    clock.now += 400U;
    feed(engine, request(protocol::MessageType::MotorFrameCommand, 6U,
                         sessionPayload(kSession1, motorFramePayload(stale_frame)),
                         clock.now - 400U));
    reply = decodeSent(transport.sent.back());
    CHECK(reply.header.message_type == protocol::MessageType::CommandNack);
    CHECK(actuator.last_motor_frame == frame_before);
    CHECK(actuator.motor_frame_calls == frame_calls_before);
    CHECK(engine.statistics().freshness_rejects >= 1U);

    // Fresh command with the same sequence is accepted because the stale frame
    // never committed the sequence number.
    clock.now += 10U;
    feed(engine, request(protocol::MessageType::StartTelemetry, 6U,
                         sessionPayload(kSession1), clock.now));
    CHECK(publisher.enabled());
    domain::SensorSample sample{};
    sample.sensor_id = domain::SensorId::Ms5611;
    sample.field_count = 1U;
    sample.fields[0] = domain::make_unsigned_field(domain::FieldId::CompensatedPressurePascal, 101325U);
    CHECK(publisher.publish(sample));
    CHECK(decodeSent(transport.sent.back()).header.message_type == protocol::MessageType::SensorSample);

    // Device info is sourced from runtime facts supplied by the composition root.
    clock.now += 10U;
    feed(engine, request(protocol::MessageType::DeviceInfoRequest, 7U, {}, clock.now));
    reply = decodeSent(transport.sent.back());
    CHECK(reply.header.message_type == protocol::MessageType::DeviceInfoResponse);
    CHECK(reply.payload_size == 20U);
    CHECK(reply.payload[0] == protocol::kProtocolVersion);
    CHECK(reply.payload[16] == 4U && reply.payload[17] == 2U);
    CHECK(reply.payload[18] == 1U && reply.payload[19] == 1U);

    // The extended status preserves the six legacy bytes and explicitly reports every
    // rangefinder role disabled/unknown when no array is composed.
    clock.now += 10U;
    feed(engine, request(protocol::MessageType::DeviceStatusRequest, 8U, {}, clock.now));
    reply = decodeSent(transport.sent.back());
    CHECK(reply.header.message_type == protocol::MessageType::DeviceStatusResponse);
    CHECK(reply.payload_size == 10U);
    CHECK(reply.payload[0] == static_cast<std::uint8_t>(safety::SafetyState::Armed));
    CHECK(reply.payload[3] == 1U);
    for (std::size_t instance = 0U; instance < 4U; ++instance) {
        CHECK(reply.payload[6U + instance] == 0U);
    }

    clock.now += 10U;
    feed(engine, request(protocol::MessageType::Disarm, 9U, {}, clock.now));
    CHECK(safety.state() == safety::SafetyState::Disarmed);
    CHECK(!actuator.outputsEnabled());

    // Parser rejects corruption and resynchronizes on the next valid frame.
    auto corrupt = request(protocol::MessageType::Ping, 10U, sync_token, clock.now);
    CHECK(corrupt.size() > 4U);
    corrupt[corrupt.size() - 3U] ^= 0x01U;
    const auto rejected_before = engine.statistics().frames_rejected;
    feed(engine, corrupt);
    CHECK(engine.statistics().frames_rejected > rejected_before);
    clock.now += 10U;
    feed(engine, request(protocol::MessageType::Ping, 10U, sync_token, clock.now));
    CHECK(decodeSent(transport.sent.back()).header.message_type == protocol::MessageType::Pong);

    // Reconnect creates a new protocol-session boundary: stale session-1 control bytes
    // remain invalid even after the new session performs a fresh time sync.
    engine.setConnected(false);
    CHECK(!publisher.enabled());
    CHECK(safety.state() == safety::SafetyState::Failsafe);
    clock.now += 10U;
    engine.setConnected(true, kSession2);
    feed(engine, request(protocol::MessageType::TimeSyncRequest, 1U, sync_token, clock.now));
    CHECK(engine.timeSynchronized());
    clock.now += 10U;
    feed(engine, request(protocol::MessageType::Heartbeat, 2U,
                         sessionPayload(kSession1), clock.now));
    reply = decodeSent(transport.sent.back());
    CHECK(reply.header.message_type == protocol::MessageType::CommandNack);
    CHECK(!safety.canArmNow(clock.now));

    clock.now += 10U;
    feed(engine, request(protocol::MessageType::Heartbeat, 2U,
                         sessionPayload(kSession2), clock.now));
    CHECK(decodeSent(transport.sent.back()).header.message_type == protocol::MessageType::HeartbeatAck);
    return true;
}

bool testMotorFrameHasOneApplyAndOneReplyWithAllOrSafeFailure() {
    constexpr std::uint64_t kSession = UINT64_C(0x1020304050607080);
    const safety::QuadMotorPulseFrame expected{{1000U, 1250U, 1750U, 2000U}};

    {
        FakeClock clock{};
        FakeTransport transport{};
        FakeI2c bus{};
        FakeActuator actuator{};
        safety::SafetySupervisor safety{actuator, safety::SafetyConfig{1'000'000U}};
        CHECK(safety.completeInitialization(clock.now));
        link::DeviceFrameSender sender{transport, clock};
        link::SensorTelemetryPublisher publisher{sender};
        sensors::scheduler::SharedSensorScheduler sensors{bus, clock, publisher};
        command::CommandDispatcher dispatcher{safety, command::DispatcherConfig{}};
        link::ProtocolEngine engine{dispatcher, safety, actuator, sensors, publisher, sender,
                                    clock, {}, {250U, 20U}};
        engine.setConnected(true, kSession);

        const std::vector<std::uint8_t> sync_payload(8U, 0x5AU);
        feed(engine, request(protocol::MessageType::TimeSyncRequest, 1U,
                             sync_payload, clock.now));
        clock.now += 10U;
        feed(engine, request(protocol::MessageType::Heartbeat, 2U,
                             sessionPayload(kSession), clock.now));
        clock.now += 10U;
        feed(engine, request(protocol::MessageType::ArmRequest, 3U,
                             sessionPayload(kSession), clock.now));
        CHECK(safety.state() == safety::SafetyState::Armed);

        // The production/default dispatcher rejects both individual-motor
        // wire paths before they refresh safety timestamps or reach hardware.
        const auto valid_frame_before = safety.freshness().valid_frame.monotonic_us;
        const auto valid_control_before =
            safety.freshness().valid_control_command.monotonic_us;
        const auto legacy_replies_before = transport.sent.size();
        clock.now += 10U;
        feed(engine, request(protocol::MessageType::MotorCommand, 4U,
                             sessionPayload(kSession, {0U, 0xE8U, 0x03U}),
                             clock.now));
        CHECK(transport.sent.size() == legacy_replies_before + 1U);
        CHECK(decodeSent(transport.sent.back()).header.message_type ==
              protocol::MessageType::CommandNack);
        CHECK(actuator.pulse_write_calls == 0U);
        CHECK(actuator.motor_frame_calls == 0U);
        CHECK(safety.freshness().valid_frame.monotonic_us == valid_frame_before);
        CHECK(safety.freshness().valid_control_command.monotonic_us ==
              valid_control_before);

        const auto replies_before = transport.sent.size();
        clock.now += 10U;
        feed(engine, request(protocol::MessageType::MotorFrameCommand, 4U,
                             sessionPayload(kSession, motorFramePayload(expected)),
                             clock.now));
        CHECK(actuator.motor_frame_calls == 1U);
        CHECK(actuator.pulse_write_calls == 0U);
        CHECK(actuator.last_motor_frame == expected);
        CHECK(actuator.outputsEnabled());
        CHECK(transport.sent.size() == replies_before + 1U);
        const auto reply = decodeSent(transport.sent.back());
        CHECK(reply.header.message_type == protocol::MessageType::CommandAck);
        CHECK(reply.payload_size == 5U);
        CHECK(reply.payload[4] == 18U);
    }

    {
        FakeClock clock{};
        FakeTransport transport{};
        FakeI2c bus{};
        FakeActuator actuator{};
        safety::SafetySupervisor safety{actuator, safety::SafetyConfig{1'000'000U}};
        CHECK(safety.completeInitialization(clock.now));
        link::DeviceFrameSender sender{transport, clock};
        link::SensorTelemetryPublisher publisher{sender};
        sensors::scheduler::SharedSensorScheduler sensors{bus, clock, publisher};
        command::CommandDispatcher dispatcher{safety, command::DispatcherConfig{}};
        link::ProtocolEngine engine{dispatcher, safety, actuator, sensors, publisher, sender,
                                    clock, {}, {250U, 20U}};
        engine.setConnected(true, kSession);

        const std::vector<std::uint8_t> sync_payload(8U, 0xA5U);
        feed(engine, request(protocol::MessageType::TimeSyncRequest, 1U,
                             sync_payload, clock.now));
        clock.now += 10U;
        feed(engine, request(protocol::MessageType::Heartbeat, 2U,
                             sessionPayload(kSession), clock.now));
        clock.now += 10U;
        feed(engine, request(protocol::MessageType::ArmRequest, 3U,
                             sessionPayload(kSession), clock.now));
        CHECK(safety.state() == safety::SafetyState::Armed);

        actuator.motor_frame_result = safety::ActuatorStatus::HardwareError;
        const auto replies_before = transport.sent.size();
        clock.now += 10U;
        feed(engine, request(protocol::MessageType::MotorFrameCommand, 4U,
                             sessionPayload(kSession, motorFramePayload(expected)),
                             clock.now));
        CHECK(actuator.motor_frame_calls == 1U);
        CHECK(actuator.pulse_write_calls == 0U);
        CHECK(!actuator.armed());
        CHECK(!actuator.outputsEnabled());
        CHECK(actuator.last_safe_reason == safety::SafeStopReason::Fault);
        CHECK(safety.state() == safety::SafetyState::Fault);
        CHECK(safety.faultReason() == safety::FaultReason::ActuatorHardwareFailure);
        CHECK(transport.sent.size() == replies_before + 1U);
        const auto reply = decodeSent(transport.sent.back());
        CHECK(reply.header.message_type == protocol::MessageType::CommandNack);
        CHECK(reply.payload_size == 8U);
        CHECK(reply.payload[4] ==
              static_cast<std::uint8_t>(protocol::NackReason::InvalidState));
        CHECK(reply.payload[6] ==
              static_cast<std::uint8_t>(command::ValidationError::ActuatorRejected));
    }
    return true;
}

}  // namespace

int main() {
    if (!testSenderAndPublisher() || !testTimeResyncRepairsAgedMapping() ||
        !testProtocolEndToEnd() ||
        !testMotorFrameHasOneApplyAndOneReplyWithAllOrSafeFailure()) return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
