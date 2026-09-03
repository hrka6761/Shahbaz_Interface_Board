/**
 * @file espidf_sensor_shutdown_bank.hpp
 * @brief Fail-closed ESP-IDF GPIO adapter for four active-low XSHUT lines.
 */
#pragma once

#include "shahbaz/interfaces/sensor_shutdown_bank.hpp"

#include <array>
#include <cstdint>

namespace shahbaz::platform {

struct EspIdfSensorShutdownConfig final {
    std::array<std::int32_t, 4U> gpio{{-1, -1, -1, -1}};
    std::int32_t i2c_sda_gpio{8};
    std::int32_t i2c_scl_gpio{9};
    bool exact_routes_reviewed{};
};

class EspIdfSensorShutdownBank final : public interfaces::ISensorShutdownBank {
  public:
    explicit EspIdfSensorShutdownBank(EspIdfSensorShutdownConfig config) noexcept
        : config_(config) {}
    ~EspIdfSensorShutdownBank() override;

    EspIdfSensorShutdownBank(const EspIdfSensorShutdownBank&) = delete;
    auto operator=(const EspIdfSensorShutdownBank&)
        -> EspIdfSensorShutdownBank& = delete;

    [[nodiscard]] auto initialize() noexcept -> interfaces::ShutdownStatus;
    [[nodiscard]] auto initialized() const noexcept -> bool { return initialized_; }
    [[nodiscard]] auto channel_count() const noexcept -> std::size_t override;
    [[nodiscard]] auto set_enabled(std::size_t index, bool enabled) noexcept
        -> interfaces::ShutdownStatus override;
    [[nodiscard]] auto disable_all() noexcept
        -> interfaces::ShutdownStatus override;

  private:
    [[nodiscard]] auto authorized() const noexcept -> bool;

    EspIdfSensorShutdownConfig config_{};
    bool initialized_{};
};

}  // namespace shahbaz::platform
