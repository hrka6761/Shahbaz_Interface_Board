#pragma once

#include "shahbaz/safety/actuator_controller.hpp"

#include <cstdint>

namespace shahbaz::actuator {

class NullActuatorController final : public safety::IActuatorController {
  public:
    void forceSafe(safety::SafeStopReason reason) noexcept override;
    [[nodiscard]] auto arm() noexcept -> safety::ActuatorStatus override {
        return safety::ActuatorStatus::Unavailable;
    }
    [[nodiscard]] auto writePulseUs(safety::ActuatorKind,
                                    std::uint8_t,
                                    std::uint16_t) noexcept
        -> safety::ActuatorStatus override {
        return safety::ActuatorStatus::Unavailable;
    }
    [[nodiscard]] auto writeMotorFrame(
        const safety::QuadMotorPulseFrame&) noexcept
        -> safety::ActuatorStatus override {
        return safety::ActuatorStatus::Unavailable;
    }
    [[nodiscard]] auto outputsEnabled() const noexcept -> bool override { return false; }
    [[nodiscard]] auto armed() const noexcept -> bool override { return false; }
    [[nodiscard]] auto available() const noexcept -> bool override { return false; }

    [[nodiscard]] auto safeRequestCount() const noexcept -> std::uint32_t {
        return safe_request_count_;
    }
    [[nodiscard]] auto lastReason() const noexcept -> safety::SafeStopReason {
        return last_reason_;
    }

  private:
    std::uint32_t safe_request_count_{0U};
    safety::SafeStopReason last_reason_{safety::SafeStopReason::Startup};
};

}  // namespace shahbaz::actuator
