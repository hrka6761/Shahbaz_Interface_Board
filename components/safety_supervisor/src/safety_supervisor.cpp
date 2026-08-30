#include "shahbaz/safety/safety_supervisor.hpp"

#include <limits>

namespace shahbaz::safety {

SafetySupervisor::SafetySupervisor(IActuatorController& actuator,
                                   SafetyConfig config) noexcept
    : actuator_(actuator), config_(config) {
    if (config_.heartbeat_timeout_us == 0U) {
        config_.heartbeat_timeout_us = 1U;
    }
    if (config_.control_command_timeout_us == 0U) {
        config_.control_command_timeout_us = 1U;
    }
    reset(0U);
}

void SafetySupervisor::reset(const std::uint64_t now_us) noexcept {
    state_ = SafetyState::Booting;
    fault_reason_ = FaultReason::None;
    freshness_ = {};
    counters_ = {};
    initialization_time_ = {};
    connection_epoch_ = {};
    last_event_time_us_ = now_us;
    initialized_ = false;
    usb_connected_ = false;
    ever_connected_ = false;
    emergency_latched_ = false;
    fault_latched_ = false;
    heartbeat_required_for_recovery_ = false;
    actuator_.forceSafe(SafeStopReason::Startup);
}

auto SafetySupervisor::completeInitialization(const std::uint64_t now_us) noexcept -> bool {
    if (!acceptTimestamp(now_us) || initialized_) {
        return false;
    }
    initialized_ = true;
    initialization_time_ = {now_us, true};
    if (!fault_latched_ && !emergency_latched_) {
        transitionToSafe(SafetyState::Disarmed, SafeStopReason::Startup);
    }
    return true;
}

auto SafetySupervisor::setUsbConnected(const bool connected,
                                       const std::uint64_t now_us) noexcept -> bool {
    if (!acceptTimestamp(now_us)) {
        return false;
    }
    if (connected == usb_connected_) {
        return true;
    }
    usb_connected_ = connected;
    if (connected) {
        const bool reconnect = ever_connected_;
        if (reconnect) {
            incrementSaturating(counters_.usb_reconnects);
        }
        ever_connected_ = true;
        freshness_ = {};
        connection_epoch_ = {now_us, true};
        // A new logical CDC session must prove liveness with a fresh heartbeat.
        // Only the very first connection is allowed to remain simply Disarmed.
        heartbeat_required_for_recovery_ = reconnect;
    } else if (ever_connected_) {
        heartbeat_required_for_recovery_ = true;
        transitionToSafe(SafetyState::Failsafe, SafeStopReason::LinkFailsafe);
    }
    return true;
}

auto SafetySupervisor::observeUsbTraffic(const std::uint64_t now_us) noexcept -> bool {
    return usb_connected_ && updateStamp(freshness_.usb_traffic, now_us);
}

auto SafetySupervisor::observeValidFrame(const std::uint64_t now_us) noexcept -> bool {
    return observeValidFrame(now_us, now_us);
}

auto SafetySupervisor::observeValidFrame(const std::uint64_t received_us,
                                         const std::uint64_t observed_us) noexcept -> bool {
    return usb_connected_ && updateReceivedStamp(freshness_.valid_frame, received_us, observed_us);
}

auto SafetySupervisor::observeValidHeartbeat(const std::uint64_t now_us) noexcept -> bool {
    return observeValidHeartbeat(now_us, now_us);
}

auto SafetySupervisor::observeValidHeartbeat(const std::uint64_t received_us,
                                             const std::uint64_t observed_us) noexcept -> bool {
    if (!usb_connected_ || !updateReceivedStamp(freshness_.valid_heartbeat,
                                                received_us,
                                                observed_us)) {
        return false;
    }
    heartbeat_required_for_recovery_ = false;
    if (state_ == SafetyState::Failsafe && !fault_latched_ && !emergency_latched_) {
        transitionToSafe(SafetyState::Disarmed, SafeStopReason::ExplicitDisarm);
    }
    return true;
}

auto SafetySupervisor::observeValidControlCommand(const std::uint64_t now_us) noexcept -> bool {
    return observeValidControlCommand(now_us, now_us);
}

auto SafetySupervisor::observeValidControlCommand(const std::uint64_t received_us,
                                                  const std::uint64_t observed_us) noexcept -> bool {
    return usb_connected_ && updateReceivedStamp(freshness_.valid_control_command,
                                                 received_us,
                                                 observed_us);
}

void SafetySupervisor::reportCrcError() noexcept { incrementSaturating(counters_.crc_errors); }
void SafetySupervisor::reportParserError() noexcept { incrementSaturating(counters_.parser_errors); }
void SafetySupervisor::reportInvalidCommand() noexcept { incrementSaturating(counters_.invalid_commands); }
void SafetySupervisor::reportHardwareMismatch() noexcept { latchFault(FaultReason::HardwareMismatch); }
void SafetySupervisor::reportSafetyQueueOverflow() noexcept { latchFault(FaultReason::SafetyQueueOverflow); }
void SafetySupervisor::reportCriticalTaskFailure() noexcept { latchFault(FaultReason::CriticalTaskFailure); }
void SafetySupervisor::reportWatchdogFailure() noexcept { latchFault(FaultReason::WatchdogFailure); }
void SafetySupervisor::reportActuatorFailure() noexcept { latchFault(FaultReason::ActuatorHardwareFailure); }

auto SafetySupervisor::handleDisarm(const std::uint64_t now_us) noexcept -> SafetyCommandResult {
    if (!acceptTimestamp(now_us)) {
        transitionToSafe(SafetyState::Fault, SafeStopReason::Fault);
        return SafetyCommandResult::AcceptedSafeState;
    }
    actuator_.forceSafe(SafeStopReason::ExplicitDisarm);
    if (!fault_latched_ && !emergency_latched_) {
        state_ = initialized_ ? SafetyState::Disarmed : SafetyState::Booting;
    }
    return SafetyCommandResult::AcceptedSafeState;
}

auto SafetySupervisor::handleEmergencyStop(const std::uint64_t now_us) noexcept -> SafetyCommandResult {
    (void)acceptTimestamp(now_us);
    emergency_latched_ = true;
    transitionToSafe(SafetyState::EmergencyStopped, SafeStopReason::EmergencyStop);
    return SafetyCommandResult::AcceptedSafeState;
}

auto SafetySupervisor::canArmNow(const std::uint64_t now_us) const noexcept -> bool {
    return initialized_ && !fault_latched_ && !emergency_latched_ && usb_connected_ &&
           state_ == SafetyState::Disarmed && actuator_.available() &&
           communicationState(now_us) == CommunicationState::Healthy;
}

auto SafetySupervisor::handleArmRequest(const std::uint64_t now_us) noexcept -> SafetyCommandResult {
    if (!acceptTimestamp(now_us)) {
        incrementSaturating(counters_.rejected_arm_commands);
        transitionToSafe(SafetyState::Fault, SafeStopReason::Fault);
        return SafetyCommandResult::RejectedInvalidState;
    }
    if (!actuator_.available()) {
        incrementSaturating(counters_.rejected_arm_commands);
        actuator_.forceSafe(SafeStopReason::ArmRejected);
        return SafetyCommandResult::RejectedActuatorUnavailable;
    }
    if (state_ != SafetyState::Disarmed || fault_latched_ || emergency_latched_ || !initialized_) {
        incrementSaturating(counters_.rejected_arm_commands);
        actuator_.forceSafe(SafeStopReason::ArmRejected);
        return SafetyCommandResult::RejectedInvalidState;
    }
    if (!usb_connected_ || communicationState(now_us) != CommunicationState::Healthy) {
        incrementSaturating(counters_.rejected_arm_commands);
        actuator_.forceSafe(SafeStopReason::ArmRejected);
        return SafetyCommandResult::RejectedLinkUnhealthy;
    }
    state_ = SafetyState::Arming;
    const auto status = actuator_.arm();
    if (status != ActuatorStatus::Ok || !actuator_.armed()) {
        incrementSaturating(counters_.rejected_arm_commands);
        actuator_.forceSafe(SafeStopReason::ArmRejected);
        state_ = SafetyState::Disarmed;
        if (status == ActuatorStatus::HardwareError) {
            latchFault(FaultReason::ActuatorHardwareFailure);
            return SafetyCommandResult::RejectedActuatorFailure;
        }
        return SafetyCommandResult::RejectedActuatorUnavailable;
    }
    state_ = SafetyState::Armed;
    return SafetyCommandResult::AcceptedArmed;
}

auto SafetySupervisor::handleActuatorCommand(const std::uint64_t now_us) noexcept -> SafetyCommandResult {
    if (!acceptTimestamp(now_us)) {
        incrementSaturating(counters_.rejected_actuator_commands);
        transitionToSafe(SafetyState::Fault, SafeStopReason::Fault);
        return SafetyCommandResult::RejectedInvalidState;
    }
    if (state_ != SafetyState::Armed || !actuator_.armed()) {
        incrementSaturating(counters_.rejected_actuator_commands);
        return SafetyCommandResult::RejectedInvalidState;
    }
    if (!usb_connected_ || heartbeatExpired(now_us)) {
        incrementSaturating(counters_.rejected_actuator_commands);
        heartbeat_required_for_recovery_ = true;
        transitionToSafe(SafetyState::Failsafe, SafeStopReason::LinkFailsafe);
        return SafetyCommandResult::RejectedLinkUnhealthy;
    }
    return SafetyCommandResult::AcceptedActuatorCommand;
}

auto SafetySupervisor::evaluate(const std::uint64_t now_us) noexcept -> SafetyState {
    if (!acceptTimestamp(now_us)) {
        transitionToSafe(SafetyState::Fault, SafeStopReason::Fault);
        return state_;
    }
    if (emergency_latched_) {
        transitionToSafe(SafetyState::EmergencyStopped, SafeStopReason::EmergencyStop);
        return state_;
    }
    if (fault_latched_) {
        transitionToSafe(SafetyState::Fault, SafeStopReason::Fault);
        return state_;
    }
    if (!initialized_) {
        transitionToSafe(SafetyState::Booting, SafeStopReason::Startup);
        return state_;
    }
    if ((state_ != SafetyState::Armed && state_ != SafetyState::Arming) &&
        (actuator_.armed() || actuator_.outputsEnabled())) {
        latchFault(FaultReason::ActuatorInvariantViolation);
        return state_;
    }
    if (state_ == SafetyState::Armed && !actuator_.armed()) {
        latchFault(FaultReason::ActuatorInvariantViolation);
        return state_;
    }
    if (ever_connected_ && !usb_connected_) {
        heartbeat_required_for_recovery_ = true;
        transitionToSafe(SafetyState::Failsafe, SafeStopReason::LinkFailsafe);
        return state_;
    }
    if (usb_connected_ && heartbeatExpired(now_us)) {
        if (!heartbeat_required_for_recovery_) {
            incrementSaturating(counters_.missed_heartbeats);
        }
        heartbeat_required_for_recovery_ = true;
        transitionToSafe(SafetyState::Failsafe, SafeStopReason::LinkFailsafe);
        return state_;
    }
    if (heartbeat_required_for_recovery_) {
        transitionToSafe(SafetyState::Failsafe, SafeStopReason::LinkFailsafe);
        return state_;
    }
    if (state_ == SafetyState::Armed && controlCommandExpired(now_us)) {
        incrementSaturating(counters_.missed_control_commands);
        transitionToSafe(SafetyState::Failsafe, SafeStopReason::ControlCommandTimeout);
        return state_;
    }
    if (state_ == SafetyState::Armed || state_ == SafetyState::Arming) {
        return state_;
    }
    state_ = SafetyState::Disarmed;
    actuator_.forceSafe(SafeStopReason::ExplicitDisarm);
    return state_;
}

auto SafetySupervisor::communicationState(const std::uint64_t now_us) const noexcept -> CommunicationState {
    if (!usb_connected_) {
        return CommunicationState::Disconnected;
    }
    if (!stampBelongsToCurrentSession(freshness_.valid_frame)) {
        return stampBelongsToCurrentSession(freshness_.usb_traffic)
                   ? CommunicationState::TrafficPresentButInvalid
                   : CommunicationState::ConnectedInactive;
    }
    if (!stampBelongsToCurrentSession(freshness_.valid_heartbeat) ||
        now_us < freshness_.valid_heartbeat.monotonic_us ||
        (now_us - freshness_.valid_heartbeat.monotonic_us) >= config_.heartbeat_timeout_us) {
        return CommunicationState::HeartbeatMissing;
    }
    return CommunicationState::Healthy;
}

auto SafetySupervisor::acceptTimestamp(const std::uint64_t now_us) noexcept -> bool {
    if (now_us < last_event_time_us_) {
        latchFault(FaultReason::MonotonicClockRegression);
        return false;
    }
    last_event_time_us_ = now_us;
    return true;
}

auto SafetySupervisor::updateStamp(FreshnessStamp& stamp, const std::uint64_t now_us) noexcept -> bool {
    if (!acceptTimestamp(now_us)) {
        return false;
    }
    stamp = {now_us, true};
    return true;
}

auto SafetySupervisor::updateReceivedStamp(FreshnessStamp& stamp,
                                           const std::uint64_t received_us,
                                           const std::uint64_t observed_us) noexcept -> bool {
    if (received_us > observed_us || !acceptTimestamp(observed_us)) {
        return false;
    }
    if (!connection_epoch_.observed || received_us < connection_epoch_.monotonic_us) {
        return false;
    }
    if (stamp.observed && received_us < stamp.monotonic_us) {
        return false;
    }
    stamp = {received_us, true};
    return true;
}

auto SafetySupervisor::heartbeatExpired(const std::uint64_t now_us) const noexcept -> bool {
    if (!usb_connected_) {
        return false;
    }
    const FreshnessStamp& reference =
        stampBelongsToCurrentSession(freshness_.valid_heartbeat)
            ? freshness_.valid_heartbeat
            : connection_epoch_;
    if (!reference.observed || now_us < reference.monotonic_us) {
        return true;
    }
    return (now_us - reference.monotonic_us) >= config_.heartbeat_timeout_us;
}

auto SafetySupervisor::controlCommandExpired(const std::uint64_t now_us) const noexcept -> bool {
    if (state_ != SafetyState::Armed) {
        return false;
    }
    const FreshnessStamp& reference =
        stampBelongsToCurrentSession(freshness_.valid_control_command)
            ? freshness_.valid_control_command
            : connection_epoch_;
    if (!reference.observed || now_us < reference.monotonic_us) {
        return true;
    }
    return (now_us - reference.monotonic_us) >= config_.control_command_timeout_us;
}

auto SafetySupervisor::stampBelongsToCurrentSession(const FreshnessStamp& stamp) const noexcept -> bool {
    return stamp.observed && connection_epoch_.observed &&
           stamp.monotonic_us >= connection_epoch_.monotonic_us;
}

void SafetySupervisor::latchFault(const FaultReason reason) noexcept {
    if (!fault_latched_) {
        fault_reason_ = reason;
    }
    fault_latched_ = true;
    transitionToSafe(SafetyState::Fault, SafeStopReason::Fault);
}

void SafetySupervisor::transitionToSafe(const SafetyState next,
                                        const SafeStopReason reason) noexcept {
    state_ = next;
    actuator_.forceSafe(reason);
}

void SafetySupervisor::incrementSaturating(std::uint32_t& value) noexcept {
    if (value != std::numeric_limits<std::uint32_t>::max()) {
        ++value;
    }
}

}  // namespace shahbaz::safety
