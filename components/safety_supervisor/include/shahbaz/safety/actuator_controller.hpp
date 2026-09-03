#pragma once

/**
 * @file actuator_controller.hpp
 * @brief Safety-owned actuator abstraction for PWM ESC/servo backends.
 */

#include <array>
#include <cstddef>
#include <cstdint>

namespace shahbaz::safety {

enum class SafeStopReason : std::uint8_t {
    Startup,
    ExplicitDisarm,
    ArmRejected,
    ActuatorCommandRejected,
    ControlCommandTimeout,
    LinkFailsafe,
    Fault,
    EmergencyStop,
};

enum class ActuatorStatus : std::uint8_t {
    Ok = 0,
    Unavailable,
    NotInitialized,
    NotArmed,
    InvalidChannel,
    InvalidValue,
    HardwareError,
};

enum class ActuatorKind : std::uint8_t {
    Motor = 1U,
    Servo = 2U,
};

inline constexpr std::size_t kQuadMotorCount = 4U;
using QuadMotorPulseFrame = std::array<std::uint16_t, kQuadMotorCount>;

/**
 * Physical-output port owned by the safety supervisor/application composition.
 * Implementations must fail safe: every error must leave the addressed output
 * unchanged or safer, and forceSafe() must be idempotent.
 */
class IActuatorController {
  public:
    virtual ~IActuatorController() = default;

    virtual void forceSafe(SafeStopReason reason) noexcept = 0;
    [[nodiscard]] virtual auto arm() noexcept -> ActuatorStatus = 0;
    [[nodiscard]] virtual auto writePulseUs(ActuatorKind kind,
                                            std::uint8_t channel,
                                            std::uint16_t pulse_us) noexcept
        -> ActuatorStatus = 0;
    /**
     * Applies one complete Quad-X motor generation. Implementations must
     * validate every value before changing an output and must force every
     * output safe if staging or applying any channel fails.
     */
    [[nodiscard]] virtual auto writeMotorFrame(
        const QuadMotorPulseFrame& pulse_us) noexcept -> ActuatorStatus = 0;
    [[nodiscard]] virtual auto outputsEnabled() const noexcept -> bool = 0;
    [[nodiscard]] virtual auto armed() const noexcept -> bool = 0;
    [[nodiscard]] virtual auto available() const noexcept -> bool = 0;
};

}  // namespace shahbaz::safety
