#include "shahbaz/actuator/espidf_pwm_actuator_controller.hpp"

#include "shahbaz/actuator/pwm_math.hpp"
#include "shahbaz/board/board_profile.hpp"

#include "driver/ledc.h"
#include "esp_err.h"

#include <array>
#include <cstddef>

namespace shahbaz::actuator {
namespace {
constexpr auto kMode = LEDC_LOW_SPEED_MODE;
constexpr auto kResolution = LEDC_TIMER_13_BIT;
constexpr unsigned kResolutionBits = 13U;
constexpr std::array<ledc_channel_t, 4U> kMotorChannels{{LEDC_CHANNEL_0, LEDC_CHANNEL_1,
                                                         LEDC_CHANNEL_2, LEDC_CHANNEL_3}};
constexpr std::array<ledc_channel_t, 2U> kServoChannels{{LEDC_CHANNEL_4, LEDC_CHANNEL_5}};

[[nodiscard]] auto safeOutputPin(const int pin) noexcept -> bool {
    return shahbaz::board::classify_gpio(pin) == shahbaz::board::PinClassification::AvailableWithReview;
}
}  // namespace

auto EspIdfPwmActuatorController::pinConfigurationValid() const noexcept -> bool {
    std::array<int, 6U> pins{};
    for (std::size_t i = 0U; i < config_.motor_gpio.size(); ++i) pins[i] = config_.motor_gpio[i];
    for (std::size_t i = 0U; i < config_.servo_gpio.size(); ++i) pins[4U + i] = config_.servo_gpio[i];
    for (std::size_t i = 0U; i < pins.size(); ++i) {
        if (!safeOutputPin(pins[i])) return false;
        for (std::size_t j = i + 1U; j < pins.size(); ++j) {
            if (pins[i] == pins[j]) return false;
        }
    }
    return config_.motor_frequency_hz >= 50U && config_.motor_frequency_hz <= 500U &&
           config_.servo_frequency_hz >= 40U && config_.servo_frequency_hz <= 400U;
}

auto EspIdfPwmActuatorController::configureChannels() noexcept -> bool {
    ledc_timer_config_t motor_timer{};
    motor_timer.speed_mode = kMode;
    motor_timer.duty_resolution = kResolution;
    motor_timer.timer_num = LEDC_TIMER_0;
    motor_timer.freq_hz = config_.motor_frequency_hz;
    motor_timer.clk_cfg = LEDC_AUTO_CLK;
    if (ledc_timer_config(&motor_timer) != ESP_OK) return false;

    ledc_timer_config_t servo_timer{};
    servo_timer.speed_mode = kMode;
    servo_timer.duty_resolution = kResolution;
    servo_timer.timer_num = LEDC_TIMER_1;
    servo_timer.freq_hz = config_.servo_frequency_hz;
    servo_timer.clk_cfg = LEDC_AUTO_CLK;
    if (ledc_timer_config(&servo_timer) != ESP_OK) return false;

    for (std::size_t i = 0U; i < kMotorChannels.size(); ++i) {
        ledc_channel_config_t channel{};
        channel.gpio_num = config_.motor_gpio[i];
        channel.speed_mode = kMode;
        channel.channel = kMotorChannels[i];
        channel.timer_sel = LEDC_TIMER_0;
        channel.duty = 0U;
        channel.hpoint = 0;
        if (ledc_channel_config(&channel) != ESP_OK || ledc_stop(kMode, kMotorChannels[i], 0U) != ESP_OK) {
            return false;
        }
    }
    for (std::size_t i = 0U; i < kServoChannels.size(); ++i) {
        ledc_channel_config_t channel{};
        channel.gpio_num = config_.servo_gpio[i];
        channel.speed_mode = kMode;
        channel.channel = kServoChannels[i];
        channel.timer_sel = LEDC_TIMER_1;
        channel.duty = 0U;
        channel.hpoint = 0;
        if (ledc_channel_config(&channel) != ESP_OK || ledc_stop(kMode, kServoChannels[i], 0U) != ESP_OK) {
            return false;
        }
    }
    return true;
}

auto EspIdfPwmActuatorController::initialize() noexcept -> bool {
    if (initialized_) return true;
    if (!pinConfigurationValid() || !configureChannels()) {
        forceSafe(safety::SafeStopReason::Fault);
        return false;
    }
    initialized_ = true;
    armed_ = false;
    outputs_enabled_ = false;
    return true;
}

void EspIdfPwmActuatorController::forceSafe(const safety::SafeStopReason) noexcept {
    // Before initialize() succeeds no LEDC timer/channel belongs to this
    // controller. Keep the software state safe without touching peripherals so
    // board validation can happen before any physical-output initialization.
    if (initialized_) {
        for (const auto channel : kMotorChannels) (void)ledc_stop(kMode, channel, 0U);
        for (const auto channel : kServoChannels) (void)ledc_stop(kMode, channel, 0U);
    }
    outputs_enabled_ = false;
    armed_ = false;
}

auto EspIdfPwmActuatorController::arm() noexcept -> safety::ActuatorStatus {
    if (!initialized_) return safety::ActuatorStatus::NotInitialized;
    // Arming grants permission but deliberately leaves every output stopped.
    armed_ = true;
    outputs_enabled_ = false;
    return safety::ActuatorStatus::Ok;
}

auto EspIdfPwmActuatorController::writePulseUs(const safety::ActuatorKind kind,
                                               const std::uint8_t channel,
                                               const std::uint16_t pulse_us) noexcept
    -> safety::ActuatorStatus {
    if (!initialized_) return safety::ActuatorStatus::NotInitialized;
    if (!armed_) return safety::ActuatorStatus::NotArmed;

    ledc_channel_t ledc_channel{};
    std::uint32_t frequency = 0U;
    if (kind == safety::ActuatorKind::Motor) {
        if (channel >= static_cast<std::uint8_t>(kMotorChannels.size()) || pulse_us < 900U || pulse_us > 2100U) {
            return channel >= static_cast<std::uint8_t>(kMotorChannels.size()) ? safety::ActuatorStatus::InvalidChannel
                                                    : safety::ActuatorStatus::InvalidValue;
        }
        ledc_channel = kMotorChannels[channel];
        frequency = config_.motor_frequency_hz;
    } else if (kind == safety::ActuatorKind::Servo) {
        if (channel >= static_cast<std::uint8_t>(kServoChannels.size()) || pulse_us < 500U || pulse_us > 2500U) {
            return channel >= static_cast<std::uint8_t>(kServoChannels.size()) ? safety::ActuatorStatus::InvalidChannel
                                                    : safety::ActuatorStatus::InvalidValue;
        }
        ledc_channel = kServoChannels[channel];
        frequency = config_.servo_frequency_hz;
    } else {
        return safety::ActuatorStatus::InvalidValue;
    }

    const auto duty = pulse_us_to_duty(pulse_us, frequency, kResolutionBits);
    if (ledc_set_duty(kMode, ledc_channel, duty) != ESP_OK ||
        ledc_update_duty(kMode, ledc_channel) != ESP_OK) {
        forceSafe(safety::SafeStopReason::Fault);
        return safety::ActuatorStatus::HardwareError;
    }
    outputs_enabled_ = true;
    return safety::ActuatorStatus::Ok;
}

}  // namespace shahbaz::actuator
