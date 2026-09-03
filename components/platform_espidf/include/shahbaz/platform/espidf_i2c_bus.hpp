#pragma once

#include "shahbaz/interfaces/i2c_bus.hpp"

#include "driver/i2c_master.h"

#include <array>
#include <cstdint>

namespace shahbaz::platform {

struct EspIdfI2cBusConfig final {
    int sda_gpio{8};
    int scl_gpio{9};
    std::uint32_t clock_hz{400'000U};
    bool enable_internal_pullups{true};
    bool alternate_pins_physically_reviewed{false};
};

/** ESP-IDF master-bus adapter preserving the project's explicit STOP/repeated-START semantics. */
class EspIdfI2cBus final : public interfaces::II2cBus {
  public:
    explicit EspIdfI2cBus(EspIdfI2cBusConfig config) noexcept : config_(config) {}
    ~EspIdfI2cBus() override;

    EspIdfI2cBus(const EspIdfI2cBus&) = delete;
    auto operator=(const EspIdfI2cBus&) -> EspIdfI2cBus& = delete;

    [[nodiscard]] auto initialize() noexcept -> interfaces::I2cStatus;
    [[nodiscard]] auto initialized() const noexcept -> bool { return bus_ != nullptr; }

    [[nodiscard]] auto transfer(const interfaces::I2cTransaction& transaction) noexcept
        -> interfaces::I2cStatus override;
    [[nodiscard]] auto recover(std::uint32_t timeout_us) noexcept
        -> interfaces::I2cStatus override;

  private:
    struct DeviceSlot final {
        std::uint8_t address{};
        i2c_master_dev_handle_t handle{};
        bool used{};
    };

    [[nodiscard]] auto configurationAuthorized() const noexcept -> bool;
    [[nodiscard]] auto deviceFor(std::uint8_t address) noexcept -> i2c_master_dev_handle_t;
    [[nodiscard]] auto restoreAfterTimedOutRecovery() noexcept -> interfaces::I2cStatus;
    void removeDevices() noexcept;
    void shutdown() noexcept;
    [[nodiscard]] static auto mapError(esp_err_t error) noexcept -> interfaces::I2cStatus;
    [[nodiscard]] static auto timeoutMs(std::uint32_t timeout_us) noexcept -> int;

    EspIdfI2cBusConfig config_{};
    i2c_master_bus_handle_t bus_{};
    // SHT30 + MS5611 + VL53L0X default address + four assigned addresses.
    std::array<DeviceSlot, 8U> devices_{};
};

}  // namespace shahbaz::platform
