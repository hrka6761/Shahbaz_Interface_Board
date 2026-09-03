#pragma once

#include "shahbaz/safety/actuator_controller.hpp"

#include <array>
#include <cstdint>

namespace shahbaz::actuator {

struct EspIdfPwmActuatorConfig final {
    std::array<int, 4U> motor_gpio{{-1, -1, -1, -1}};
    std::array<int, 2U> servo_gpio{{-1, -1}};
    std::uint32_t motor_frequency_hz{400U};
    std::uint32_t servo_frequency_hz{50U};
};

/** LEDC-backed conventional PWM ESC/servo controller; outputs default electrically stopped. */
class EspIdfPwmActuatorController final : public safety::IActuatorController {
  public:
    explicit EspIdfPwmActuatorController(EspIdfPwmActuatorConfig config) noexcept : config_(config) {}

    [[nodiscard]] auto initialize() noexcept -> bool;
    void forceSafe(safety::SafeStopReason reason) noexcept override;
    [[nodiscard]] auto arm() noexcept -> safety::ActuatorStatus override;
    [[nodiscard]] auto writePulseUs(safety::ActuatorKind kind,
                                    std::uint8_t channel,
                                    std::uint16_t pulse_us) noexcept -> safety::ActuatorStatus override;
    [[nodiscard]] auto writeMotorFrame(
        const safety::QuadMotorPulseFrame& pulse_us) noexcept
        -> safety::ActuatorStatus override;
    [[nodiscard]] auto outputsEnabled() const noexcept -> bool override { return outputs_enabled_; }
    [[nodiscard]] auto armed() const noexcept -> bool override { return armed_; }
    [[nodiscard]] auto available() const noexcept -> bool override { return initialized_; }

  private:
    [[nodiscard]] auto configureChannels() noexcept -> bool;
    [[nodiscard]] auto pinConfigurationValid() const noexcept -> bool;

    EspIdfPwmActuatorConfig config_{};
    bool initialized_{};
    bool armed_{};
    bool outputs_enabled_{};
};

}  // namespace shahbaz::actuator
