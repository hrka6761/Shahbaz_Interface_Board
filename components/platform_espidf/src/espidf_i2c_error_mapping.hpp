#pragma once

#include "shahbaz/interfaces/i2c_bus.hpp"

#include "esp_err.h"

namespace shahbaz::platform::detail {

[[nodiscard]] constexpr auto map_i2c_error(const esp_err_t error) noexcept
    -> interfaces::I2cStatus {
    switch (error) {
        case ESP_OK:
            return interfaces::I2cStatus::Ok;
        case ESP_ERR_INVALID_ARG:
            return interfaces::I2cStatus::InvalidArgument;
        // The ESP-IDF synchronous I2C driver also returns INVALID_STATE when
        // a transaction finishes in a non-DONE state, including a target NACK.
        // Local bus_ == nullptr is handled before entering the driver, so this
        // result must not be reported as an uninitialized adapter.
        case ESP_ERR_INVALID_STATE:
            return interfaces::I2cStatus::BusError;
        case ESP_ERR_NOT_FOUND:
            return interfaces::I2cStatus::NotFound;
        case ESP_ERR_TIMEOUT:
            return interfaces::I2cStatus::Timeout;
        default:
            return interfaces::I2cStatus::BusError;
    }
}

}  // namespace shahbaz::platform::detail
