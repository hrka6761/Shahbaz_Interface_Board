/**
 * @file espidf_sensor_shutdown_bank.cpp
 * @brief ESP32-S3 XSHUT GPIO implementation.
 */
#include "shahbaz/platform/espidf_sensor_shutdown_bank.hpp"

#include "shahbaz/board/board_profile.hpp"

#include "driver/gpio.h"

namespace shahbaz::platform {

EspIdfSensorShutdownBank::~EspIdfSensorShutdownBank() {
    if (initialized_) (void)disable_all();
}

auto EspIdfSensorShutdownBank::authorized() const noexcept -> bool {
    return config_.exact_routes_reviewed &&
           board::is_valid_vl53l0x_xshut_pins(
               config_.gpio, config_.i2c_sda_gpio, config_.i2c_scl_gpio);
}

auto EspIdfSensorShutdownBank::initialize() noexcept
    -> interfaces::ShutdownStatus {
    if (initialized_) return interfaces::ShutdownStatus::Ok;
    if (!authorized()) return interfaces::ShutdownStatus::InvalidArgument;

    std::uint64_t mask = 0U;
    for (const auto pin : config_.gpio) {
        // Program the output latch low before enabling output direction so an
        // XSHUT line cannot briefly release during boot.
        if (gpio_set_level(static_cast<gpio_num_t>(pin), 0) != ESP_OK) {
            return interfaces::ShutdownStatus::HardwareError;
        }
        mask |= 1ULL << static_cast<unsigned>(pin);
    }
    gpio_config_t pins_config{};
    pins_config.pin_bit_mask = mask;
    pins_config.mode = GPIO_MODE_OUTPUT;
    pins_config.pull_up_en = GPIO_PULLUP_DISABLE;
    pins_config.pull_down_en = GPIO_PULLDOWN_ENABLE;
    pins_config.intr_type = GPIO_INTR_DISABLE;
    if (gpio_config(&pins_config) != ESP_OK) {
        return interfaces::ShutdownStatus::HardwareError;
    }
    initialized_ = true;
    const auto status = disable_all();
    if (status != interfaces::ShutdownStatus::Ok) initialized_ = false;
    return status;
}

auto EspIdfSensorShutdownBank::channel_count() const noexcept -> std::size_t {
    return config_.gpio.size();
}

auto EspIdfSensorShutdownBank::set_enabled(const std::size_t index,
                                           const bool enabled) noexcept
    -> interfaces::ShutdownStatus {
    if (!initialized_) return interfaces::ShutdownStatus::NotInitialized;
    if (index >= config_.gpio.size()) {
        return interfaces::ShutdownStatus::InvalidArgument;
    }
    return gpio_set_level(static_cast<gpio_num_t>(config_.gpio[index]), enabled ? 1 : 0) ==
                   ESP_OK
               ? interfaces::ShutdownStatus::Ok
               : interfaces::ShutdownStatus::HardwareError;
}

auto EspIdfSensorShutdownBank::disable_all() noexcept
    -> interfaces::ShutdownStatus {
    if (!initialized_) return interfaces::ShutdownStatus::NotInitialized;
    bool ok = true;
    for (const auto pin : config_.gpio) {
        if (gpio_set_level(static_cast<gpio_num_t>(pin), 0) != ESP_OK) ok = false;
    }
    return ok ? interfaces::ShutdownStatus::Ok
              : interfaces::ShutdownStatus::HardwareError;
}

}  // namespace shahbaz::platform
