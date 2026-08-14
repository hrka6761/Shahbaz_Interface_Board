#pragma once

/**
 * @file safety_supervisor_interface.hpp
 * @brief Transport/application-facing safety policy port.
 */

#include "shahbaz/safety/safety_types.hpp"

#include <cstdint>

namespace shahbaz::safety {

/**
 * Narrow port required by command validation.
 *
 * It exposes safe-state operations and receipt-time freshness only. It cannot
 * authorize arming or physical output. Board/task/watchdog composition may use
 * the concrete supervisor's additional lifecycle and fault-reporting API.
 */
class ISafetySupervisor {
  public:
    virtual ~ISafetySupervisor() = default;

    [[nodiscard]] virtual auto state() const noexcept -> SafetyState = 0;
    [[nodiscard]] virtual auto observeValidFrame(
        std::uint64_t received_us,
        std::uint64_t observed_us) noexcept -> bool = 0;
    [[nodiscard]] virtual auto observeValidHeartbeat(
        std::uint64_t received_us,
        std::uint64_t observed_us) noexcept -> bool = 0;
    [[nodiscard]] virtual auto observeValidControlCommand(
        std::uint64_t received_us,
        std::uint64_t observed_us) noexcept -> bool = 0;

    virtual void reportInvalidCommand() noexcept = 0;
    [[nodiscard]] virtual auto handleDisarm(std::uint64_t now_us) noexcept
        -> SafetyCommandResult = 0;
    [[nodiscard]] virtual auto handleEmergencyStop(
        std::uint64_t now_us) noexcept -> SafetyCommandResult = 0;
    [[nodiscard]] virtual auto handleArmRequest(std::uint64_t now_us) noexcept
        -> SafetyCommandResult = 0;
    [[nodiscard]] virtual auto handleActuatorCommand(
        std::uint64_t now_us) noexcept -> SafetyCommandResult = 0;
};

}  // namespace shahbaz::safety

