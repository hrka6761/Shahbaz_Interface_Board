#include "shahbaz/command/command_dispatcher.hpp"
#include "shahbaz/safety/safety_supervisor.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <type_traits>

namespace {

#define CHECK(condition)                                                               \
    do {                                                                               \
        if (!(condition)) {                                                            \
            std::fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__,   \
                         #condition);                                                  \
            return false;                                                              \
        }                                                                              \
    } while (false)

using namespace shahbaz;

class FakeSafeController final : public safety::IActuatorController {
public:
    void forceSafe(safety::SafeStopReason reason) noexcept override {
        ++safe_calls;
        last_reason = reason;
        is_armed = false;
        outputs_enabled = false;
    }
    [[nodiscard]] auto arm() noexcept -> safety::ActuatorStatus override {
        if (!is_available) {
            return safety::ActuatorStatus::Unavailable;
        }
        is_armed = true;
        return safety::ActuatorStatus::Ok;
    }
    [[nodiscard]] auto writePulseUs(safety::ActuatorKind kind,
                                    std::uint8_t channel,
                                    std::uint16_t pulse_us) noexcept
        -> safety::ActuatorStatus override {
        if (!is_armed) {
            return safety::ActuatorStatus::NotArmed;
        }
        last_kind = kind;
        last_channel = channel;
        last_pulse_us = pulse_us;
        outputs_enabled = true;
        ++write_calls;
        return safety::ActuatorStatus::Ok;
    }
    [[nodiscard]] bool outputsEnabled() const noexcept override { return outputs_enabled; }
    [[nodiscard]] bool armed() const noexcept override { return is_armed; }
    [[nodiscard]] bool available() const noexcept override { return is_available; }

    bool is_available{true};
    bool is_armed{false};
    bool outputs_enabled{false};
    std::uint32_t safe_calls{0U};
    std::uint32_t write_calls{0U};
    safety::ActuatorKind last_kind{safety::ActuatorKind::Motor};
    std::uint8_t last_channel{0U};
    std::uint16_t last_pulse_us{0U};
    safety::SafeStopReason last_reason{safety::SafeStopReason::Startup};
};

constexpr command::DispatcherConfig kConfig{
    100U,
    command::IntervalBounds{100U, 1000U},
    command::IntervalBounds{10U, 500U},
};

struct Fixture final {
    FakeSafeController actuator{};
    safety::SafetySupervisor safety_supervisor{actuator, safety::SafetyConfig{10000U}};
    command::CommandDispatcher dispatcher{safety_supervisor, kConfig};

    Fixture() {
        (void)safety_supervisor.completeInitialization(0U);
        (void)safety_supervisor.setUsbConnected(true, 1U);
    }
};

protocol::DecodedFrame makeFrame(
    protocol::MessageType type,
    std::uint32_t sequence,
    const std::uint8_t* payload,
    std::size_t payload_size) {
    protocol::DecodedFrame frame{};
    frame.header = protocol::makeHeader(
        type,
        protocol::MessagePriority::High,
        sequence,
        static_cast<std::uint64_t>(sequence) * 10U,
        static_cast<std::uint16_t>(payload_size));
    frame.payload_size = payload_size;
    if (payload_size <= frame.payload.size()) {
        for (std::size_t i = 0U; i < payload_size; ++i) {
            frame.payload[i] = payload[i];
        }
    }
    return frame;
}

protocol::DecodedFrame makeEmptyFrame(
    protocol::MessageType type,
    std::uint32_t sequence) {
    return makeFrame(type, sequence, nullptr, 0U);
}

command::DispatchContext fresh(
    std::uint64_t received_us,
    std::uint64_t dispatch_us) {
    return {received_us, dispatch_us, command::SenderFreshness::Fresh};
}

bool testHeartbeatFreshnessIsIndependent() {
    Fixture fixture{};
    const auto heartbeat = makeEmptyFrame(protocol::MessageType::Heartbeat, 1U);
    const command::DispatchResult accepted = fixture.dispatcher.dispatch(
        heartbeat,
        fresh(2U, 2U));
    CHECK(accepted.accepted);
    CHECK(accepted.reply == command::ReplyKind::HeartbeatAck);
    CHECK(accepted.action == command::ApplicationAction::HeartbeatReceived);
    CHECK(accepted.frame_freshness_updated);
    CHECK(accepted.heartbeat_freshness_updated);
    CHECK(!accepted.control_freshness_updated);
    CHECK(fixture.safety_supervisor.freshness().valid_frame.monotonic_us == 2U);
    CHECK(fixture.safety_supervisor.freshness().valid_heartbeat.monotonic_us == 2U);
    CHECK(!fixture.safety_supervisor.freshness().valid_control_command.observed);
    return true;
}

bool testInvalidAndStaleHeartbeatsNeverRefreshHeartbeat() {
    Fixture fixture{};
    CHECK(fixture.dispatcher.dispatch(
              makeEmptyFrame(protocol::MessageType::Heartbeat, 10U),
              fresh(2U, 2U))
              .accepted);
    CHECK(fixture.safety_supervisor.freshness().valid_heartbeat.monotonic_us == 2U);

    constexpr std::uint8_t invalid_payload[] = {0x01U};
    const command::DispatchResult invalid_length = fixture.dispatcher.dispatch(
        makeFrame(protocol::MessageType::Heartbeat,
                  11U,
                  invalid_payload,
                  sizeof(invalid_payload)),
        fresh(3U, 3U));
    CHECK(!invalid_length.accepted);
    CHECK(invalid_length.validation_error ==
          command::ValidationError::InvalidPayloadLength);
    CHECK(invalid_length.frame_freshness_updated);
    CHECK(!invalid_length.heartbeat_freshness_updated);
    CHECK(fixture.safety_supervisor.freshness().valid_heartbeat.monotonic_us == 2U);

    const command::DispatchResult local_stale = fixture.dispatcher.dispatch(
        makeEmptyFrame(protocol::MessageType::Heartbeat, 11U),
        fresh(4U, 105U));
    CHECK(!local_stale.accepted);
    CHECK(local_stale.nack_reason == protocol::NackReason::StaleOrExpired);
    CHECK(local_stale.validation_error == command::ValidationError::LocalDispatchExpired);
    CHECK(!local_stale.heartbeat_freshness_updated);
    CHECK(fixture.safety_supervisor.freshness().valid_heartbeat.monotonic_us == 2U);

    const command::DispatchResult sender_stale = fixture.dispatcher.dispatch(
        makeEmptyFrame(protocol::MessageType::Heartbeat, 11U),
        {106U, 106U, command::SenderFreshness::StaleOrExpired});
    CHECK(!sender_stale.accepted);
    CHECK(sender_stale.validation_error ==
          command::ValidationError::SenderStaleOrExpired);
    CHECK(!sender_stale.heartbeat_freshness_updated);

    const command::DispatchResult unsynchronized = fixture.dispatcher.dispatch(
        makeEmptyFrame(protocol::MessageType::Heartbeat, 11U),
        {107U, 107U, command::SenderFreshness::NotSynchronized});
    CHECK(!unsynchronized.accepted);
    CHECK(unsynchronized.validation_error ==
          command::ValidationError::SenderTimeNotSynchronized);
    CHECK(!unsynchronized.heartbeat_freshness_updated);
    CHECK(fixture.safety_supervisor.freshness().valid_heartbeat.monotonic_us == 2U);
    return true;
}

bool testDuplicateAndOutOfOrderHeartbeatsDoNotRefresh() {
    Fixture fixture{};
    CHECK(fixture.dispatcher.dispatch(
              makeEmptyFrame(protocol::MessageType::Heartbeat, 10U),
              fresh(2U, 2U))
              .accepted);

    const command::DispatchResult duplicate = fixture.dispatcher.dispatch(
        makeEmptyFrame(protocol::MessageType::Heartbeat, 10U),
        fresh(3U, 3U));
    CHECK(!duplicate.accepted);
    CHECK(duplicate.nack_reason == protocol::NackReason::DuplicateSequence);
    CHECK(duplicate.validation_error == command::ValidationError::DuplicateSequence);
    CHECK(!duplicate.heartbeat_freshness_updated);

    const command::DispatchResult out_of_order = fixture.dispatcher.dispatch(
        makeEmptyFrame(protocol::MessageType::Heartbeat, 9U),
        fresh(4U, 4U));
    CHECK(!out_of_order.accepted);
    CHECK(out_of_order.nack_reason == protocol::NackReason::OutOfOrderSequence);
    CHECK(out_of_order.validation_error == command::ValidationError::OutOfOrderSequence);
    CHECK(!out_of_order.heartbeat_freshness_updated);
    CHECK(fixture.safety_supervisor.freshness().valid_heartbeat.monotonic_us == 2U);

    const command::DispatchResult next = fixture.dispatcher.dispatch(
        makeEmptyFrame(protocol::MessageType::Heartbeat, 11U),
        fresh(5U, 5U));
    CHECK(next.accepted);
    CHECK(next.heartbeat_freshness_updated);
    CHECK(fixture.safety_supervisor.freshness().valid_heartbeat.monotonic_us == 5U);
    return true;
}

bool testPhysicalControlSchemasAndSafetyGating() {
    Fixture fixture{};

    constexpr std::uint8_t motor[] = {0U, 0xB0U, 0x04U};  // channel 0, 1200 us
    command::DispatchResult result = fixture.dispatcher.dispatch(
        makeFrame(protocol::MessageType::MotorCommand, 1U, motor, sizeof(motor)),
        fresh(2U, 2U));
    CHECK(!result.accepted);
    CHECK(result.validation_error == command::ValidationError::InvalidSafetyState);
    CHECK(!result.control_freshness_updated);

    CHECK(fixture.safety_supervisor.observeValidHeartbeat(3U));
    result = fixture.dispatcher.dispatch(
        makeEmptyFrame(protocol::MessageType::ArmRequest, 2U),
        fresh(4U, 4U));
    CHECK(result.accepted);
    CHECK(result.action == command::ApplicationAction::ArmApplied);
    CHECK(result.control_freshness_updated);
    CHECK(fixture.safety_supervisor.state() == safety::SafetyState::Armed);
    CHECK(fixture.actuator.armed());

    result = fixture.dispatcher.dispatch(
        makeFrame(protocol::MessageType::MotorCommand, 3U, motor, sizeof(motor)),
        fresh(5U, 5U));
    CHECK(result.accepted);
    CHECK(result.action == command::ApplicationAction::MotorCommand);
    CHECK(result.actuator.present);
    CHECK(result.actuator.kind == safety::ActuatorKind::Motor);
    CHECK(result.actuator.channel == 0U);
    CHECK(result.actuator.pulse_us == 1200U);
    CHECK(result.control_freshness_updated);

    constexpr std::uint8_t servo[] = {1U, 0xDCU, 0x05U};  // channel 1, 1500 us
    result = fixture.dispatcher.dispatch(
        makeFrame(protocol::MessageType::ServoCommand, 4U, servo, sizeof(servo)),
        fresh(6U, 6U));
    CHECK(result.accepted);
    CHECK(result.action == command::ApplicationAction::ServoCommand);
    CHECK(result.actuator.kind == safety::ActuatorKind::Servo);
    CHECK(result.actuator.channel == 1U);
    CHECK(result.actuator.pulse_us == 1500U);

    constexpr std::uint8_t generic[] = {
        static_cast<std::uint8_t>(safety::ActuatorKind::Motor), 2U, 0xE8U, 0x03U
    };  // motor 2, 1000 us
    result = fixture.dispatcher.dispatch(
        makeFrame(protocol::MessageType::ActuatorCommand, 5U, generic, sizeof(generic)),
        fresh(7U, 7U));
    CHECK(result.accepted);
    CHECK(result.action == command::ApplicationAction::ActuatorCommand);
    CHECK(result.actuator.channel == 2U);
    CHECK(result.actuator.pulse_us == 1000U);

    constexpr std::uint8_t mode[] = {0U};
    result = fixture.dispatcher.dispatch(
        makeFrame(protocol::MessageType::SetControlMode, 6U, mode, sizeof(mode)),
        fresh(8U, 8U));
    CHECK(result.accepted);
    CHECK(result.action == command::ApplicationAction::SetControlMode);
    CHECK(result.control_mode.present);
    CHECK(result.control_mode.mode == 0U);

    constexpr std::uint8_t bad_motor_channel[] = {4U, 0xB0U, 0x04U};
    result = fixture.dispatcher.dispatch(
        makeFrame(protocol::MessageType::MotorCommand, 7U,
                  bad_motor_channel, sizeof(bad_motor_channel)),
        fresh(9U, 9U));
    CHECK(!result.accepted);
    CHECK(result.validation_error == command::ValidationError::InvalidPayloadValue);

    constexpr std::uint8_t bad_servo_pulse[] = {0U, 0xF4U, 0x01U};  // 500 exactly valid
    result = fixture.dispatcher.dispatch(
        makeFrame(protocol::MessageType::ServoCommand, 7U,
                  bad_servo_pulse, sizeof(bad_servo_pulse)),
        fresh(10U, 10U));
    CHECK(result.accepted);
    return true;
}

bool testEmergencyStopAndDisarmAlwaysApply() {
    {
        Fixture fixture{};
        constexpr std::uint8_t noncanonical[] = {0xDEU, 0xADU};
        const command::DispatchResult result = fixture.dispatcher.dispatch(
            makeFrame(protocol::MessageType::EmergencyStop,
                      1U,
                      noncanonical,
                      sizeof(noncanonical)),
            {20U, 10U, command::SenderFreshness::StaleOrExpired});
        CHECK(result.accepted);
        CHECK(result.reply == command::ReplyKind::CommandAck);
        CHECK(result.action == command::ApplicationAction::EmergencyStopApplied);
        CHECK(result.noncanonical_safe_payload_ignored);
        CHECK(!result.frame_freshness_updated);
        CHECK(!result.control_freshness_updated);
        CHECK(!result.heartbeat_freshness_updated);
        CHECK(fixture.safety_supervisor.state() == safety::SafetyState::EmergencyStopped);
        CHECK(!fixture.dispatcher.hasAcceptedSequence());
    }
    {
        Fixture fixture{};
        auto disarm = makeEmptyFrame(protocol::MessageType::Disarm, 1U);
        disarm.header.version = static_cast<std::uint8_t>(protocol::kProtocolVersion + 1U);
        const command::DispatchResult result = fixture.dispatcher.dispatch(
            disarm,
            {2U, 2U, command::SenderFreshness::StaleOrExpired});
        CHECK(result.accepted);
        CHECK(result.action == command::ApplicationAction::DisarmApplied);
        CHECK(result.noncanonical_safe_payload_ignored);
        CHECK(!result.frame_freshness_updated);
        CHECK(!result.control_freshness_updated);
        CHECK(fixture.safety_supervisor.state() == safety::SafetyState::Disarmed);
    }
    {
        Fixture fixture{};
        const command::DispatchResult result = fixture.dispatcher.dispatch(
            makeEmptyFrame(protocol::MessageType::Disarm, 99U),
            {2U, 2U, command::SenderFreshness::Fresh});
        CHECK(result.accepted);
        CHECK(result.reply == command::ReplyKind::CommandAck);
        CHECK(result.action == command::ApplicationAction::DisarmApplied);
        CHECK(!result.noncanonical_safe_payload_ignored);
        CHECK(result.frame_freshness_updated);
        CHECK(result.control_freshness_updated);
        CHECK(!result.heartbeat_freshness_updated);
        CHECK(fixture.safety_supervisor.state() == safety::SafetyState::Disarmed);
        CHECK(fixture.dispatcher.hasAcceptedSequence());
        CHECK(fixture.dispatcher.lastAcceptedSequence() == 99U);
    }
    return true;
}

bool testHeartbeatAgeUsesReceiptNotDispatchTime() {
    Fixture fixture{};
    const command::DispatchResult result = fixture.dispatcher.dispatch(
        makeEmptyFrame(protocol::MessageType::Heartbeat, 1U),
        {10U, 100U, command::SenderFreshness::Fresh});
    CHECK(result.accepted);
    CHECK(result.frame_freshness_updated);
    CHECK(result.heartbeat_freshness_updated);
    CHECK(fixture.safety_supervisor.freshness().valid_frame.monotonic_us == 10U);
    CHECK(fixture.safety_supervisor.freshness().valid_heartbeat.monotonic_us == 10U);
    return true;
}

bool testProvisionalEmptyAndTokenSchemas() {
    Fixture fixture{};
    command::DispatchResult result = fixture.dispatcher.dispatch(
        makeEmptyFrame(protocol::MessageType::DeviceInfoRequest, 1U),
        {2U, 2U, command::SenderFreshness::NotSynchronized});
    CHECK(result.accepted);
    CHECK(result.reply == command::ReplyKind::DeviceInfoResponse);

    result = fixture.dispatcher.dispatch(
        makeEmptyFrame(protocol::MessageType::DeviceStatusRequest, 2U),
        {3U, 3U, command::SenderFreshness::NotSynchronized});
    CHECK(result.accepted);
    CHECK(result.reply == command::ReplyKind::DeviceStatusResponse);

    result = fixture.dispatcher.dispatch(
        makeEmptyFrame(protocol::MessageType::StartTelemetry, 3U),
        fresh(4U, 4U));
    CHECK(result.accepted);
    CHECK(result.action == command::ApplicationAction::StartTelemetry);
    CHECK(!result.heartbeat_freshness_updated);
    CHECK(!result.control_freshness_updated);

    result = fixture.dispatcher.dispatch(
        makeEmptyFrame(protocol::MessageType::StopTelemetry, 4U),
        fresh(5U, 5U));
    CHECK(result.accepted);
    CHECK(result.action == command::ApplicationAction::StopTelemetry);

    constexpr std::uint8_t token[] = {1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U};
    result = fixture.dispatcher.dispatch(
        makeFrame(protocol::MessageType::Ping, 5U, token, sizeof(token)),
        {6U, 6U, command::SenderFreshness::NotSynchronized});
    CHECK(result.accepted);
    CHECK(result.reply == command::ReplyKind::Pong);

    result = fixture.dispatcher.dispatch(
        makeFrame(protocol::MessageType::TimeSyncRequest, 6U, token, sizeof(token)),
        {7U, 7U, command::SenderFreshness::NotSynchronized});
    CHECK(result.accepted);
    CHECK(result.reply == command::ReplyKind::TimeSyncResponse);

    result = fixture.dispatcher.dispatch(
        makeFrame(protocol::MessageType::Ping, 7U, token, sizeof(token) - 1U),
        fresh(8U, 8U));
    CHECK(!result.accepted);
    CHECK(result.validation_error == command::ValidationError::InvalidPayloadLength);
    CHECK(fixture.dispatcher.lastAcceptedSequence() == 6U);
    return true;
}

bool testSetSensorRateSchemaAndInjectedBounds() {
    Fixture fixture{};
    constexpr std::uint8_t valid_sht3x[] = {
        static_cast<std::uint8_t>(command::SensorId::Sht3x),
        0U,
        0xF4U,
        0x01U,
        0x00U,
        0x00U,
    };  // 500 us, within the injected test range.
    command::DispatchResult result = fixture.dispatcher.dispatch(
        makeFrame(protocol::MessageType::SetSensorRate,
                  1U,
                  valid_sht3x,
                  sizeof(valid_sht3x)),
        fresh(2U, 2U));
    CHECK(result.accepted);
    CHECK(result.action == command::ApplicationAction::SetSensorRate);
    CHECK(result.sensor_rate.present);
    CHECK(result.sensor_rate.sensor_id == command::SensorId::Sht3x);
    CHECK(result.sensor_rate.instance_id == 0U);
    CHECK(result.sensor_rate.interval_us == 500U);

    constexpr std::uint8_t valid_ms5611[] = {
        static_cast<std::uint8_t>(command::SensorId::Ms5611),
        0U,
        0xFAU,
        0x00U,
        0x00U,
        0x00U,
    };  // 250 us, within the independently injected test range.
    result = fixture.dispatcher.dispatch(
        makeFrame(protocol::MessageType::SetSensorRate,
                  2U,
                  valid_ms5611,
                  sizeof(valid_ms5611)),
        fresh(3U, 3U));
    CHECK(result.accepted);
    CHECK(result.sensor_rate.sensor_id == command::SensorId::Ms5611);
    CHECK(result.sensor_rate.interval_us == 250U);

    constexpr std::uint8_t unknown_sensor[] = {3U, 0U, 100U, 0U, 0U, 0U};
    result = fixture.dispatcher.dispatch(
        makeFrame(protocol::MessageType::SetSensorRate,
                  3U,
                  unknown_sensor,
                  sizeof(unknown_sensor)),
        fresh(4U, 4U));
    CHECK(!result.accepted);
    CHECK(result.validation_error == command::ValidationError::InvalidPayloadValue);

    constexpr std::uint8_t wrong_instance[] = {1U, 1U, 100U, 0U, 0U, 0U};
    result = fixture.dispatcher.dispatch(
        makeFrame(protocol::MessageType::SetSensorRate,
                  3U,
                  wrong_instance,
                  sizeof(wrong_instance)),
        fresh(5U, 5U));
    CHECK(!result.accepted);
    CHECK(result.validation_error == command::ValidationError::InvalidPayloadValue);

    constexpr std::uint8_t below_bound[] = {1U, 0U, 99U, 0U, 0U, 0U};
    result = fixture.dispatcher.dispatch(
        makeFrame(protocol::MessageType::SetSensorRate,
                  3U,
                  below_bound,
                  sizeof(below_bound)),
        fresh(6U, 6U));
    CHECK(!result.accepted);
    CHECK(result.validation_error == command::ValidationError::InvalidPayloadValue);
    CHECK(fixture.dispatcher.lastAcceptedSequence() == 2U);
    return true;
}

bool testEnvelopeBoundsAndUnsupportedDirection() {
    Fixture fixture{};
    auto oversized = makeEmptyFrame(protocol::MessageType::Heartbeat, 1U);
    oversized.payload_size = protocol::kMaxPayloadLength + 1U;
    oversized.header.payload_length =
        static_cast<std::uint16_t>(protocol::kMaxPayloadLength + 1U);
    command::DispatchResult result = fixture.dispatcher.dispatch(
        oversized,
        fresh(2U, 2U));
    CHECK(!result.accepted);
    CHECK(result.validation_error == command::ValidationError::InvalidEnvelope);
    CHECK(!result.frame_freshness_updated);
    CHECK(!result.heartbeat_freshness_updated);

    auto mismatched = makeEmptyFrame(protocol::MessageType::Heartbeat, 1U);
    mismatched.header.payload_length = 1U;
    result = fixture.dispatcher.dispatch(mismatched, fresh(3U, 3U));
    CHECK(!result.accepted);
    CHECK(!result.frame_freshness_updated);

    result = fixture.dispatcher.dispatch(
        makeEmptyFrame(protocol::MessageType::DeviceInfoResponse, 1U),
        fresh(4U, 4U));
    CHECK(!result.accepted);
    CHECK(result.validation_error == command::ValidationError::UnsupportedDirection);
    CHECK(result.frame_freshness_updated);
    CHECK(!result.heartbeat_freshness_updated);
    return true;
}

bool testCommandsAreValidatedAgainstSafetyState() {
    Fixture fixture{};
    fixture.safety_supervisor.reportHardwareMismatch();
    CHECK(fixture.safety_supervisor.state() == safety::SafetyState::Fault);

    command::DispatchResult result = fixture.dispatcher.dispatch(
        makeEmptyFrame(protocol::MessageType::StartTelemetry, 1U),
        fresh(2U, 2U));
    CHECK(!result.accepted);
    CHECK(result.nack_reason == protocol::NackReason::InvalidState);
    CHECK(result.validation_error == command::ValidationError::InvalidSafetyState);
    CHECK(result.frame_freshness_updated);
    CHECK(!fixture.dispatcher.hasAcceptedSequence());
    CHECK(fixture.safety_supervisor.counters().invalid_commands == 1U);

    result = fixture.dispatcher.dispatch(
        makeEmptyFrame(protocol::MessageType::DeviceStatusRequest, 1U),
        {3U, 3U, command::SenderFreshness::NotSynchronized});
    CHECK(result.accepted);
    CHECK(result.action == command::ApplicationAction::RequestDeviceStatus);
    CHECK(fixture.dispatcher.lastAcceptedSequence() == 1U);

    result = fixture.dispatcher.dispatch(
        makeEmptyFrame(protocol::MessageType::StopTelemetry, 2U),
        fresh(4U, 4U));
    CHECK(result.accepted);
    CHECK(result.action == command::ApplicationAction::StopTelemetry);

    constexpr std::uint8_t valid_rate[] = {
        static_cast<std::uint8_t>(command::SensorId::Sht3x),
        0U,
        0xF4U,
        0x01U,
        0x00U,
        0x00U,
    };
    result = fixture.dispatcher.dispatch(
        makeFrame(protocol::MessageType::SetSensorRate,
                  3U,
                  valid_rate,
                  sizeof(valid_rate)),
        fresh(5U, 5U));
    CHECK(!result.accepted);
    CHECK(result.validation_error == command::ValidationError::InvalidSafetyState);
    CHECK(fixture.safety_supervisor.counters().invalid_commands == 2U);
    return true;
}

bool testSequenceWrapAndSessionReset() {
    Fixture fixture{};
    command::DispatchResult result = fixture.dispatcher.dispatch(
        makeEmptyFrame(protocol::MessageType::DeviceInfoRequest, UINT32_MAX),
        {2U, 2U, command::SenderFreshness::NotSynchronized});
    CHECK(result.accepted);
    result = fixture.dispatcher.dispatch(
        makeEmptyFrame(protocol::MessageType::DeviceStatusRequest, 0U),
        {3U, 3U, command::SenderFreshness::NotSynchronized});
    CHECK(result.accepted);
    CHECK(fixture.dispatcher.lastAcceptedSequence() == 0U);

    fixture.dispatcher.resetSessionSequence();
    CHECK(!fixture.dispatcher.hasAcceptedSequence());
    CHECK(fixture.safety_supervisor.state() == safety::SafetyState::Disarmed);
    result = fixture.dispatcher.dispatch(
        makeEmptyFrame(protocol::MessageType::DeviceInfoRequest, 1U),
        {4U, 4U, command::SenderFreshness::NotSynchronized});
    CHECK(result.accepted);
    CHECK(fixture.safety_supervisor.state() == safety::SafetyState::Disarmed);
    return true;
}

static_assert(std::is_trivially_copyable<command::DispatchResult>::value,
              "DispatchResult must remain allocation-free value data");

}  // namespace

int main() {
    const bool passed = testHeartbeatFreshnessIsIndependent() &&
                        testHeartbeatAgeUsesReceiptNotDispatchTime() &&
                        testInvalidAndStaleHeartbeatsNeverRefreshHeartbeat() &&
                        testDuplicateAndOutOfOrderHeartbeatsDoNotRefresh() &&
                        testPhysicalControlSchemasAndSafetyGating() &&
                        testEmergencyStopAndDisarmAlwaysApply() &&
                        testProvisionalEmptyAndTokenSchemas() &&
                        testSetSensorRateSchemaAndInjectedBounds() &&
                        testEnvelopeBoundsAndUnsupportedDirection() &&
                        testCommandsAreValidatedAgainstSafetyState() &&
                        testSequenceWrapAndSessionReset();
    return passed ? 0 : 1;
}
