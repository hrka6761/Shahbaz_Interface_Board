#include "shahbaz/safety/safety_supervisor.hpp"

#include <cstdint>
#include <cstdio>

namespace {

#define CHECK(condition)                                                               \
    do {                                                                               \
        if (!(condition)) {                                                            \
            std::fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__,   \
                         #condition);                                                  \
            return false;                                                              \
        }                                                                              \
    } while (false)

using namespace shahbaz::safety;

class FakeActuator final : public IActuatorController {
public:
    void forceSafe(SafeStopReason reason) noexcept override {
        ++safe_calls;
        last_reason = reason;
        is_armed = false;
        outputs_enabled = false;
    }

    [[nodiscard]] auto arm() noexcept -> ActuatorStatus override {
        ++arm_calls;
        if (!is_available) {
            return ActuatorStatus::Unavailable;
        }
        if (arm_result != ActuatorStatus::Ok) {
            return arm_result;
        }
        is_armed = true;
        return ActuatorStatus::Ok;
    }

    [[nodiscard]] auto writePulseUs(ActuatorKind,
                                    std::uint8_t,
                                    std::uint16_t) noexcept -> ActuatorStatus override {
        if (!is_armed) {
            return ActuatorStatus::NotArmed;
        }
        outputs_enabled = true;
        return ActuatorStatus::Ok;
    }

    [[nodiscard]] auto outputsEnabled() const noexcept -> bool override {
        return outputs_enabled;
    }
    [[nodiscard]] auto armed() const noexcept -> bool override { return is_armed; }
    [[nodiscard]] auto available() const noexcept -> bool override { return is_available; }

    bool is_available{true};
    bool is_armed{false};
    bool outputs_enabled{false};
    ActuatorStatus arm_result{ActuatorStatus::Ok};
    std::uint32_t safe_calls{0U};
    std::uint32_t arm_calls{0U};
    SafeStopReason last_reason{SafeStopReason::Startup};
};

bool makeHealthy(SafetySupervisor& supervisor, std::uint64_t now_us) {
    return supervisor.observeValidFrame(now_us) &&
           supervisor.observeValidHeartbeat(now_us);
}

bool testBootDisarmedAndUnavailableActuator() {
    FakeActuator output{};
    output.is_available = false;
    SafetySupervisor supervisor{output, SafetyConfig{100U}};
    CHECK(supervisor.state() == SafetyState::Booting);
    CHECK(output.safe_calls == 1U);
    CHECK(supervisor.completeInitialization(0U));
    CHECK(supervisor.state() == SafetyState::Disarmed);
    CHECK(supervisor.setUsbConnected(true, 1U));
    CHECK(makeHealthy(supervisor, 2U));
    CHECK(!supervisor.canArmNow(2U));
    CHECK(supervisor.handleArmRequest(2U) == SafetyCommandResult::RejectedActuatorUnavailable);
    CHECK(supervisor.state() == SafetyState::Disarmed);
    CHECK(!output.armed());
    CHECK(!output.outputsEnabled());
    CHECK(supervisor.counters().rejected_arm_commands == 1U);
    return true;
}

bool testArmingRequiresHealthyLinkAndDrivesState() {
    FakeActuator output{};
    SafetySupervisor supervisor{output, SafetyConfig{100U}};
    CHECK(supervisor.completeInitialization(0U));

    CHECK(supervisor.handleArmRequest(0U) == SafetyCommandResult::RejectedLinkUnhealthy);
    CHECK(supervisor.state() == SafetyState::Disarmed);

    CHECK(supervisor.setUsbConnected(true, 1U));
    CHECK(supervisor.observeValidFrame(2U));
    CHECK(supervisor.handleArmRequest(2U) == SafetyCommandResult::RejectedLinkUnhealthy);
    CHECK(!output.armed());

    CHECK(supervisor.observeValidHeartbeat(3U));
    CHECK(supervisor.canArmNow(3U));
    CHECK(supervisor.handleArmRequest(3U) == SafetyCommandResult::AcceptedArmed);
    CHECK(supervisor.state() == SafetyState::Armed);
    CHECK(output.armed());
    CHECK(output.arm_calls == 1U);
    CHECK(supervisor.handleActuatorCommand(4U) == SafetyCommandResult::AcceptedActuatorCommand);
    CHECK(output.writePulseUs(ActuatorKind::Motor, 0U, 1000U) == ActuatorStatus::Ok);
    CHECK(output.outputsEnabled());
    CHECK(supervisor.evaluate(50U) == SafetyState::Armed);
    return true;
}

bool testArmHardwareFailureLatchesFault() {
    FakeActuator output{};
    output.arm_result = ActuatorStatus::HardwareError;
    SafetySupervisor supervisor{output, SafetyConfig{100U}};
    CHECK(supervisor.completeInitialization(0U));
    CHECK(supervisor.setUsbConnected(true, 1U));
    CHECK(makeHealthy(supervisor, 2U));
    CHECK(supervisor.handleArmRequest(2U) == SafetyCommandResult::RejectedActuatorFailure);
    CHECK(supervisor.state() == SafetyState::Fault);
    CHECK(supervisor.faultReason() == FaultReason::ActuatorHardwareFailure);
    CHECK(!output.armed());
    CHECK(!output.outputsEnabled());
    return true;
}

bool testHeartbeatTimeoutForcesFailsafeFromArmed() {
    FakeActuator output{};
    SafetySupervisor supervisor{output, SafetyConfig{100U}};
    CHECK(supervisor.completeInitialization(0U));
    CHECK(supervisor.setUsbConnected(true, 1U));
    CHECK(makeHealthy(supervisor, 2U));
    CHECK(supervisor.handleArmRequest(2U) == SafetyCommandResult::AcceptedArmed);
    CHECK(output.writePulseUs(ActuatorKind::Motor, 0U, 1200U) == ActuatorStatus::Ok);
    CHECK(output.outputsEnabled());

    CHECK(supervisor.evaluate(101U) == SafetyState::Armed);
    CHECK(supervisor.evaluate(102U) == SafetyState::Failsafe);
    CHECK(!output.armed());
    CHECK(!output.outputsEnabled());
    CHECK(output.last_reason == SafeStopReason::LinkFailsafe);
    CHECK(supervisor.counters().missed_heartbeats == 1U);
    CHECK(supervisor.handleActuatorCommand(103U) == SafetyCommandResult::RejectedInvalidState);

    CHECK(supervisor.observeValidFrame(104U));
    CHECK(supervisor.observeValidHeartbeat(104U));
    CHECK(supervisor.state() == SafetyState::Disarmed);
    CHECK(supervisor.evaluate(104U) == SafetyState::Disarmed);
    return true;
}

bool testControlCommandTimeoutForcesFailsafeWhileHeartbeatRemainsHealthy() {
    FakeActuator output{};
    SafetySupervisor supervisor{output, SafetyConfig{1'000U, 100U}};
    CHECK(supervisor.completeInitialization(0U));
    CHECK(supervisor.setUsbConnected(true, 1U));
    CHECK(makeHealthy(supervisor, 2U));
    CHECK(supervisor.handleArmRequest(2U) == SafetyCommandResult::AcceptedArmed);
    CHECK(supervisor.observeValidControlCommand(2U));
    CHECK(supervisor.handleActuatorCommand(3U) == SafetyCommandResult::AcceptedActuatorCommand);
    CHECK(output.writePulseUs(ActuatorKind::Motor, 0U, 1200U) == ActuatorStatus::Ok);
    CHECK(supervisor.observeValidHeartbeat(90U));
    CHECK(supervisor.evaluate(101U) == SafetyState::Armed);
    CHECK(supervisor.evaluate(102U) == SafetyState::Failsafe);
    CHECK(!output.armed());
    CHECK(!output.outputsEnabled());
    CHECK(output.last_reason == SafeStopReason::ControlCommandTimeout);
    CHECK(supervisor.counters().missed_control_commands == 1U);
    return true;
}

bool testDisconnectForcesFailsafeAndReconnectNeedsHeartbeat() {
    FakeActuator output{};
    SafetySupervisor supervisor{output, SafetyConfig{100U}};
    CHECK(supervisor.completeInitialization(0U));
    CHECK(supervisor.setUsbConnected(true, 1U));
    CHECK(makeHealthy(supervisor, 2U));
    CHECK(supervisor.handleArmRequest(2U) == SafetyCommandResult::AcceptedArmed);

    CHECK(supervisor.setUsbConnected(false, 3U));
    CHECK(supervisor.state() == SafetyState::Failsafe);
    CHECK(!output.armed());
    CHECK(supervisor.setUsbConnected(true, 4U));
    CHECK(supervisor.counters().usb_reconnects == 1U);
    CHECK(supervisor.evaluate(4U) == SafetyState::Failsafe);
    CHECK(supervisor.communicationState(4U) == CommunicationState::ConnectedInactive);
    CHECK(supervisor.observeValidFrame(5U));
    CHECK(supervisor.observeValidHeartbeat(5U));
    CHECK(supervisor.state() == SafetyState::Disarmed);
    CHECK(supervisor.communicationState(5U) == CommunicationState::Healthy);
    return true;
}

bool testFreshnessIsIndependentAndUsesReceiptTimestamp() {
    FakeActuator output{};
    SafetySupervisor supervisor{output, SafetyConfig{100U}};
    CHECK(supervisor.completeInitialization(0U));
    CHECK(supervisor.setUsbConnected(true, 1U));
    CHECK(supervisor.observeUsbTraffic(10U));
    supervisor.reportCrcError();
    CHECK(supervisor.communicationState(10U) == CommunicationState::TrafficPresentButInvalid);
    CHECK(supervisor.observeValidFrame(20U, 40U));
    CHECK(supervisor.communicationState(40U) == CommunicationState::HeartbeatMissing);
    CHECK(supervisor.observeValidHeartbeat(30U, 40U));
    CHECK(supervisor.freshness().valid_frame.monotonic_us == 20U);
    CHECK(supervisor.freshness().valid_heartbeat.monotonic_us == 30U);
    CHECK(supervisor.communicationState(129U) == CommunicationState::Healthy);
    CHECK(supervisor.communicationState(130U) == CommunicationState::HeartbeatMissing);
    CHECK(!supervisor.freshness().valid_control_command.observed);
    return true;
}

bool testDisconnectedTrafficCannotSeedNewSession() {
    FakeActuator output{};
    SafetySupervisor supervisor{output, SafetyConfig{100U}};
    CHECK(supervisor.completeInitialization(0U));
    CHECK(!supervisor.observeUsbTraffic(0U));
    CHECK(!supervisor.observeValidFrame(0U));
    CHECK(!supervisor.observeValidHeartbeat(0U));
    CHECK(!supervisor.observeValidControlCommand(0U));

    CHECK(supervisor.setUsbConnected(true, 1U));
    CHECK(makeHealthy(supervisor, 1U));
    CHECK(supervisor.setUsbConnected(false, 2U));
    CHECK(!supervisor.observeValidHeartbeat(3U));
    CHECK(supervisor.setUsbConnected(true, 4U));
    CHECK(!supervisor.freshness().valid_frame.observed);
    CHECK(!supervisor.freshness().valid_heartbeat.observed);
    return true;
}

bool testEmergencyDisarmAndFaultsStaySafe() {
    FakeActuator emergency_output{};
    SafetySupervisor emergency{emergency_output, SafetyConfig{100U}};
    CHECK(emergency.completeInitialization(0U));
    CHECK(emergency.handleEmergencyStop(1U) == SafetyCommandResult::AcceptedSafeState);
    CHECK(emergency.state() == SafetyState::EmergencyStopped);
    CHECK(emergency.handleDisarm(2U) == SafetyCommandResult::AcceptedSafeState);
    CHECK(emergency.state() == SafetyState::EmergencyStopped);
    CHECK(!emergency_output.armed());

    FakeActuator fault_output{};
    SafetySupervisor fault{fault_output, SafetyConfig{100U}};
    CHECK(fault.completeInitialization(0U));
    fault.reportSafetyQueueOverflow();
    CHECK(fault.state() == SafetyState::Fault);
    CHECK(fault.faultReason() == FaultReason::SafetyQueueOverflow);
    CHECK(fault.handleDisarm(1U) == SafetyCommandResult::AcceptedSafeState);
    CHECK(fault.state() == SafetyState::Fault);
    return true;
}

bool testClockRegressionAndInvariantViolationsLatchFault() {
    {
        FakeActuator output{};
        SafetySupervisor supervisor{output, SafetyConfig{100U}};
        CHECK(supervisor.completeInitialization(10U));
        CHECK(supervisor.setUsbConnected(true, 11U));
        CHECK(supervisor.observeUsbTraffic(20U));
        CHECK(!supervisor.observeValidFrame(19U));
        CHECK(supervisor.state() == SafetyState::Fault);
        CHECK(supervisor.faultReason() == FaultReason::MonotonicClockRegression);
    }
    {
        FakeActuator output{};
        SafetySupervisor supervisor{output, SafetyConfig{100U}};
        CHECK(supervisor.completeInitialization(0U));
        output.is_armed = true;
        output.outputs_enabled = true;
        CHECK(supervisor.evaluate(1U) == SafetyState::Fault);
        CHECK(supervisor.faultReason() == FaultReason::ActuatorInvariantViolation);
        CHECK(!output.armed());
        CHECK(!output.outputsEnabled());
    }
    return true;
}

bool testEveryExplicitFaultPathLatches() {
    FakeActuator output1{};
    SafetySupervisor a{output1, SafetyConfig{100U}};
    CHECK(a.completeInitialization(0U));
    a.reportHardwareMismatch();
    CHECK(a.faultReason() == FaultReason::HardwareMismatch);

    FakeActuator output2{};
    SafetySupervisor b{output2, SafetyConfig{100U}};
    CHECK(b.completeInitialization(0U));
    b.reportCriticalTaskFailure();
    CHECK(b.faultReason() == FaultReason::CriticalTaskFailure);

    FakeActuator output3{};
    SafetySupervisor c{output3, SafetyConfig{100U}};
    CHECK(c.completeInitialization(0U));
    c.reportWatchdogFailure();
    CHECK(c.faultReason() == FaultReason::WatchdogFailure);

    FakeActuator output4{};
    SafetySupervisor d{output4, SafetyConfig{100U}};
    CHECK(d.completeInitialization(0U));
    d.reportActuatorFailure();
    CHECK(d.faultReason() == FaultReason::ActuatorHardwareFailure);
    return true;
}

}  // namespace

int main() {
    const bool passed = testBootDisarmedAndUnavailableActuator() &&
                        testArmingRequiresHealthyLinkAndDrivesState() &&
                        testArmHardwareFailureLatchesFault() &&
                        testHeartbeatTimeoutForcesFailsafeFromArmed() &&
                        testControlCommandTimeoutForcesFailsafeWhileHeartbeatRemainsHealthy() &&
                        testDisconnectForcesFailsafeAndReconnectNeedsHeartbeat() &&
                        testFreshnessIsIndependentAndUsesReceiptTimestamp() &&
                        testDisconnectedTrafficCannotSeedNewSession() &&
                        testEmergencyDisarmAndFaultsStaySafe() &&
                        testClockRegressionAndInvariantViolationsLatchFault() &&
                        testEveryExplicitFaultPathLatches();
    return passed ? 0 : 1;
}
