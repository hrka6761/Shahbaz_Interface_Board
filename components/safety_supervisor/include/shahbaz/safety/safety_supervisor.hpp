#pragma once

#include "shahbaz/safety/actuator_controller.hpp"
#include "shahbaz/safety/safety_supervisor_interface.hpp"
#include "shahbaz/safety/safety_types.hpp"

#include <cstdint>

namespace shahbaz::safety {

class SafetySupervisor final : public ISafetySupervisor {
  public:
    SafetySupervisor(IActuatorController& actuator, SafetyConfig config) noexcept;

    void reset(std::uint64_t now_us) noexcept;
    [[nodiscard]] auto completeInitialization(std::uint64_t now_us) noexcept -> bool;
    [[nodiscard]] auto setUsbConnected(bool connected, std::uint64_t now_us) noexcept -> bool;
    [[nodiscard]] auto observeUsbTraffic(std::uint64_t now_us) noexcept -> bool;
    [[nodiscard]] auto observeValidFrame(std::uint64_t now_us) noexcept -> bool;
    [[nodiscard]] auto observeValidFrame(std::uint64_t received_us,
                                         std::uint64_t observed_us) noexcept -> bool override;
    [[nodiscard]] auto observeValidHeartbeat(std::uint64_t now_us) noexcept -> bool;
    [[nodiscard]] auto observeValidHeartbeat(std::uint64_t received_us,
                                             std::uint64_t observed_us) noexcept -> bool override;
    [[nodiscard]] auto observeValidControlCommand(std::uint64_t now_us) noexcept -> bool;
    [[nodiscard]] auto observeValidControlCommand(std::uint64_t received_us,
                                                  std::uint64_t observed_us) noexcept -> bool override;

    void reportCrcError() noexcept;
    void reportParserError() noexcept;
    void reportInvalidCommand() noexcept override;
    void reportHardwareMismatch() noexcept;
    void reportSafetyQueueOverflow() noexcept;
    void reportCriticalTaskFailure() noexcept;
    void reportWatchdogFailure() noexcept;
    void reportActuatorFailure() noexcept;

    [[nodiscard]] auto handleDisarm(std::uint64_t now_us) noexcept -> SafetyCommandResult override;
    [[nodiscard]] auto handleEmergencyStop(std::uint64_t now_us) noexcept -> SafetyCommandResult override;
    [[nodiscard]] auto handleArmRequest(std::uint64_t now_us) noexcept -> SafetyCommandResult override;
    [[nodiscard]] auto handleActuatorCommand(std::uint64_t now_us) noexcept -> SafetyCommandResult override;

    [[nodiscard]] auto evaluate(std::uint64_t now_us) noexcept -> SafetyState;
    [[nodiscard]] auto communicationState(std::uint64_t now_us) const noexcept -> CommunicationState;

    [[nodiscard]] auto state() const noexcept -> SafetyState override { return state_; }
    [[nodiscard]] auto faultReason() const noexcept -> FaultReason { return fault_reason_; }
    [[nodiscard]] auto freshness() const noexcept -> const FreshnessTimestamps& { return freshness_; }
    [[nodiscard]] auto counters() const noexcept -> const SafetyCounters& { return counters_; }
    [[nodiscard]] auto usbConnected() const noexcept -> bool { return usb_connected_; }
    [[nodiscard]] auto emergencyStopLatched() const noexcept -> bool { return emergency_latched_; }
    [[nodiscard]] auto canArmNow(std::uint64_t now_us) const noexcept -> bool;

  private:
    [[nodiscard]] auto acceptTimestamp(std::uint64_t now_us) noexcept -> bool;
    [[nodiscard]] auto updateStamp(FreshnessStamp& stamp, std::uint64_t now_us) noexcept -> bool;
    [[nodiscard]] auto updateReceivedStamp(FreshnessStamp& stamp,
                                           std::uint64_t received_us,
                                           std::uint64_t observed_us) noexcept -> bool;
    [[nodiscard]] auto heartbeatExpired(std::uint64_t now_us) const noexcept -> bool;
    [[nodiscard]] auto controlCommandExpired(std::uint64_t now_us) const noexcept -> bool;
    [[nodiscard]] auto stampBelongsToCurrentSession(const FreshnessStamp& stamp) const noexcept -> bool;
    void latchFault(FaultReason reason) noexcept;
    void transitionToSafe(SafetyState next, SafeStopReason reason) noexcept;
    static void incrementSaturating(std::uint32_t& value) noexcept;

    IActuatorController& actuator_;
    SafetyConfig config_{};
    SafetyState state_{SafetyState::Booting};
    FaultReason fault_reason_{FaultReason::None};
    FreshnessTimestamps freshness_{};
    SafetyCounters counters_{};
    FreshnessStamp initialization_time_{};
    FreshnessStamp connection_epoch_{};
    std::uint64_t last_event_time_us_{0U};
    bool initialized_{false};
    bool usb_connected_{false};
    bool ever_connected_{false};
    bool emergency_latched_{false};
    bool fault_latched_{false};
    bool heartbeat_required_for_recovery_{false};
};

}  // namespace shahbaz::safety
