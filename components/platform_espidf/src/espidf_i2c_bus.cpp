#include "shahbaz/platform/espidf_i2c_bus.hpp"

#include "shahbaz/board/board_profile.hpp"
#include "espidf_i2c_error_mapping.hpp"

#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace shahbaz::platform {
namespace {

constexpr std::uint32_t kRecoveryHalfPeriodUs = 5U;
// ESP-IDF 5.4 defines -1 (rather than I2C_NUM_AUTO) as the public request for
// automatic HP I2C controller allocation.
constexpr i2c_port_num_t kAutoI2cPort = -1;

[[nodiscard]] auto elapsedAtLeast(const std::int64_t start_us,
                                  const std::uint32_t timeout_us) noexcept -> bool {
    if (timeout_us == 0U) return true;
    const auto now = esp_timer_get_time();
    return now < start_us || static_cast<std::uint64_t>(now - start_us) >= timeout_us;
}

[[nodiscard]] auto remainingUs(const std::int64_t start_us,
                               const std::uint32_t timeout_us) noexcept -> std::uint32_t {
    const auto now = esp_timer_get_time();
    if (now < start_us) return 0U;
    const auto elapsed = static_cast<std::uint64_t>(now - start_us);
    if (elapsed >= timeout_us) return 0U;
    return static_cast<std::uint32_t>(static_cast<std::uint64_t>(timeout_us) - elapsed);
}

}  // namespace

EspIdfI2cBus::~EspIdfI2cBus() { shutdown(); }

auto EspIdfI2cBus::mapError(const esp_err_t error) noexcept -> interfaces::I2cStatus {
    return detail::map_i2c_error(error);
}

auto EspIdfI2cBus::timeoutMs(const std::uint32_t timeout_us) noexcept -> int {
    // ESP-IDF accepts millisecond timeouts. Floor rather than round up so the
    // adapter never asks the driver to wait beyond the caller's microsecond bound.
    return static_cast<int>(timeout_us / 1'000U);
}

auto EspIdfI2cBus::configurationAuthorized() const noexcept -> bool {
    return board::is_valid_i2c_pair(config_.sda_gpio, config_.scl_gpio,
                                    config_.alternate_pins_physically_reviewed);
}

auto EspIdfI2cBus::initialize() noexcept -> interfaces::I2cStatus {
    if (bus_ != nullptr) return interfaces::I2cStatus::Ok;
    if (!configurationAuthorized() || config_.clock_hz == 0U || config_.clock_hz > 400'000U) {
        return interfaces::I2cStatus::InvalidArgument;
    }

    i2c_master_bus_config_t bus_config{};
    bus_config.i2c_port = kAutoI2cPort;
    bus_config.sda_io_num = static_cast<gpio_num_t>(config_.sda_gpio);
    bus_config.scl_io_num = static_cast<gpio_num_t>(config_.scl_gpio);
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7U;
    bus_config.intr_priority = 0;
    bus_config.trans_queue_depth = 0U;
    bus_config.flags.enable_internal_pullup = config_.enable_internal_pullups ? 1U : 0U;
    bus_config.flags.allow_pd = 0U;

    const auto error = i2c_new_master_bus(&bus_config, &bus_);
    if (error != ESP_OK) bus_ = nullptr;
    return mapError(error);
}

void EspIdfI2cBus::removeDevices() noexcept {
    for (auto& slot : devices_) {
        if (slot.used && slot.handle != nullptr) {
            (void)i2c_master_bus_rm_device(slot.handle);
        }
        slot = {};
    }
}

void EspIdfI2cBus::shutdown() noexcept {
    removeDevices();
    if (bus_ != nullptr) {
        (void)i2c_del_master_bus(bus_);
        bus_ = nullptr;
    }
}

auto EspIdfI2cBus::deviceFor(const std::uint8_t address) noexcept -> i2c_master_dev_handle_t {
    if (bus_ == nullptr || address > 0x7FU) return nullptr;
    for (const auto& slot : devices_) {
        if (slot.used && slot.address == address) return slot.handle;
    }
    auto* free_slot = static_cast<DeviceSlot*>(nullptr);
    for (auto& slot : devices_) {
        if (!slot.used) {
            free_slot = &slot;
            break;
        }
    }
    if (free_slot == nullptr) return nullptr;

    i2c_device_config_t config{};
    config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    config.device_address = address;
    config.scl_speed_hz = config_.clock_hz;
    config.scl_wait_us = 0U;
    i2c_master_dev_handle_t handle{};
    if (i2c_master_bus_add_device(bus_, &config, &handle) != ESP_OK) return nullptr;
    free_slot->address = address;
    free_slot->handle = handle;
    free_slot->used = true;
    return handle;
}

auto EspIdfI2cBus::transfer(const interfaces::I2cTransaction& transaction) noexcept
    -> interfaces::I2cStatus {
    if (bus_ == nullptr) return interfaces::I2cStatus::NotInitialized;
    if (transaction.seven_bit_address > 0x7FU || transaction.timeout_us == 0U ||
        !interfaces::isValid(transaction.write) || !interfaces::isValid(transaction.read) ||
        (transaction.write.size == 0U && transaction.read.size == 0U)) {
        return interfaces::I2cStatus::InvalidArgument;
    }
    const auto device = deviceFor(transaction.seven_bit_address);
    if (device == nullptr) return interfaces::I2cStatus::BusError;

    const auto started_us = esp_timer_get_time();
    esp_err_t error = ESP_OK;
    if (transaction.write.size != 0U && transaction.read.size != 0U) {
        if (transaction.write_read_boundary == interfaces::I2cWriteReadBoundary::RepeatedStart) {
            error = i2c_master_transmit_receive(device,
                                                transaction.write.data,
                                                transaction.write.size,
                                                transaction.read.data,
                                                transaction.read.size,
                                                timeoutMs(transaction.timeout_us));
        } else {
            error = i2c_master_transmit(device,
                                        transaction.write.data,
                                        transaction.write.size,
                                        timeoutMs(transaction.timeout_us));
            if (error == ESP_OK) {
                const auto remaining = remainingUs(started_us, transaction.timeout_us);
                if (remaining == 0U) return interfaces::I2cStatus::Timeout;
                error = i2c_master_receive(device,
                                           transaction.read.data,
                                           transaction.read.size,
                                           timeoutMs(remaining));
            }
        }
    } else if (transaction.write.size != 0U) {
        error = i2c_master_transmit(device,
                                    transaction.write.data,
                                    transaction.write.size,
                                    timeoutMs(transaction.timeout_us));
    } else {
        error = i2c_master_receive(device,
                                   transaction.read.data,
                                   transaction.read.size,
                                   timeoutMs(transaction.timeout_us));
    }
    if (error == ESP_OK && elapsedAtLeast(started_us, transaction.timeout_us)) {
        return interfaces::I2cStatus::Timeout;
    }
    return mapError(error);
}

auto EspIdfI2cBus::restoreAfterTimedOutRecovery() noexcept -> interfaces::I2cStatus {
    // recover() releases the ESP-IDF bus before driving the lines as GPIOs. A
    // deadline miss must not strand the shared scheduler with a null bus: only
    // preserve Timeout when driver ownership was successfully restored.
    const auto restore_status = initialize();
    return restore_status == interfaces::I2cStatus::Ok
               ? interfaces::I2cStatus::Timeout
               : interfaces::I2cStatus::RecoveryFailed;
}

auto EspIdfI2cBus::recover(const std::uint32_t timeout_us) noexcept -> interfaces::I2cStatus {
    if (timeout_us == 0U || !configurationAuthorized()) {
        return interfaces::I2cStatus::InvalidArgument;
    }
    const auto started_us = esp_timer_get_time();
    shutdown();

    gpio_config_t pins_config{};
    pins_config.pin_bit_mask = (1ULL << static_cast<unsigned>(config_.sda_gpio)) |
                               (1ULL << static_cast<unsigned>(config_.scl_gpio));
    pins_config.mode = GPIO_MODE_INPUT_OUTPUT_OD;
    pins_config.pull_up_en = GPIO_PULLUP_ENABLE;
    pins_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    pins_config.intr_type = GPIO_INTR_DISABLE;
    if (gpio_config(&pins_config) != ESP_OK) return interfaces::I2cStatus::RecoveryFailed;

    const auto sda = static_cast<gpio_num_t>(config_.sda_gpio);
    const auto scl = static_cast<gpio_num_t>(config_.scl_gpio);
    (void)gpio_set_level(sda, 1);
    (void)gpio_set_level(scl, 1);
    esp_rom_delay_us(kRecoveryHalfPeriodUs);
    if (gpio_get_level(scl) == 0) return interfaces::I2cStatus::RecoveryFailed;

    // Nine complete SCL pulses allow a target stuck mid-byte to advance to a
    // release point. Continue all nine even if SDA releases early.
    for (unsigned pulse = 0U; pulse < 9U; ++pulse) {
        if (elapsedAtLeast(started_us, timeout_us)) return restoreAfterTimedOutRecovery();
        (void)gpio_set_level(scl, 0);
        esp_rom_delay_us(kRecoveryHalfPeriodUs);
        (void)gpio_set_level(scl, 1);
        esp_rom_delay_us(kRecoveryHalfPeriodUs);
        if (gpio_get_level(scl) == 0) return interfaces::I2cStatus::RecoveryFailed;
    }

    // Explicit START then STOP with open-drain lines.
    (void)gpio_set_level(sda, 1);
    (void)gpio_set_level(scl, 1);
    esp_rom_delay_us(kRecoveryHalfPeriodUs);
    (void)gpio_set_level(sda, 0);  // START: SDA high->low while SCL high.
    esp_rom_delay_us(kRecoveryHalfPeriodUs);
    (void)gpio_set_level(scl, 0);
    esp_rom_delay_us(kRecoveryHalfPeriodUs);
    (void)gpio_set_level(sda, 0);
    (void)gpio_set_level(scl, 1);
    esp_rom_delay_us(kRecoveryHalfPeriodUs);
    if (gpio_get_level(scl) == 0) return interfaces::I2cStatus::RecoveryFailed;
    (void)gpio_set_level(sda, 1);  // STOP: SDA low->high while SCL high.
    esp_rom_delay_us(kRecoveryHalfPeriodUs);
    if (gpio_get_level(sda) == 0 || gpio_get_level(scl) == 0) {
        return interfaces::I2cStatus::RecoveryFailed;
    }
    if (elapsedAtLeast(started_us, timeout_us)) return restoreAfterTimedOutRecovery();

    const auto status = initialize();
    if (status != interfaces::I2cStatus::Ok) return interfaces::I2cStatus::RecoveryFailed;
    if (gpio_get_level(sda) == 0 || gpio_get_level(scl) == 0) {
        shutdown();
        return interfaces::I2cStatus::RecoveryFailed;
    }
    return interfaces::I2cStatus::Ok;
}

}  // namespace shahbaz::platform
