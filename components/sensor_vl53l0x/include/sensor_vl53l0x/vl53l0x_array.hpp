/**
 * @file vl53l0x_array.hpp
 * @brief Bounded, cooperative driver for four independently-shutdown VL53L0X sensors.
 */
#pragma once

#include "sensor_vl53l0x/vl53l0x_domain.hpp"
#include "shahbaz/interfaces/i2c_bus.hpp"
#include "shahbaz/interfaces/monotonic_clock.hpp"
#include "shahbaz/interfaces/sample_publisher.hpp"
#include "shahbaz/interfaces/sensor_shutdown_bank.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace shahbaz::sensors::vl53l0x {

enum class DriverError : std::uint8_t {
    None = 0,
    Disabled,
    InvalidConfiguration,
    ShutdownControl,
    I2cTransfer,
    I2cRecovery,
    UnexpectedIdentity,
    ReferenceSpadConfiguration,
    InitializationTimeout,
    MeasurementTimeout,
    InvalidResult,
    PublisherBackpressure,
};

struct DeviceHealth final {
    bool online{};
    bool configuration_valid{true};
    DriverError last_error{DriverError::None};
    interfaces::I2cStatus last_i2c_status{interfaces::I2cStatus::Ok};
    std::uint32_t consecutive_failures{};
    std::uint32_t total_failures{};
    std::uint32_t i2c_failures{};
    std::uint32_t identity_failures{};
    std::uint32_t initialization_timeouts{};
    std::uint32_t measurement_timeouts{};
    std::uint32_t recoveries{};
    std::uint32_t recovery_failures{};
    std::uint32_t shared_bus_invalidations{};
    std::uint32_t invalid_measurements{};
    std::uint32_t publication_drops{};
};

struct ArrayConfig final {
    bool enabled{};
    std::array<std::uint8_t, kSensorCount> assigned_addresses{
        kDefaultAssignedAddresses};
    std::uint32_t transaction_timeout_us{5'000U};
    std::uint32_t shutdown_settle_us{1'000U};
    std::uint32_t boot_wait_us{2'000U};
    std::uint32_t initialization_poll_interval_us{1'000U};
    std::uint32_t initialization_timeout_us{100'000U};
    std::uint32_t measurement_poll_interval_us{1'000U};
    std::uint32_t measurement_timeout_us{60'000U};
    std::uint32_t per_sensor_sample_interval_us{100'000U};
    std::uint32_t retry_initial_backoff_us{20'000U};
    std::uint32_t retry_maximum_backoff_us{1'000'000U};
    std::uint32_t bus_recovery_timeout_us{5'000U};
};

enum class ArrayState : std::uint8_t {
    Disabled = 0,
    Fault,
    HoldAllShutdown,
    WaitAfterShutdown,
    SelectWork,
    EnableDevice,
    WaitForBoot,
    ReadDefaultIdentity,
    AssignAddress,
    VerifyAssignedIdentity,
    ConfigureDevice,
    PrepareMeasurement,
    WaitForMeasurementStart,
    WaitForMeasurement,
    ReadMeasurement,
    ClearInterrupt,
    PublishMeasurement,
    RecoverBus,
    RecoverySettle,
    Idle,
};

/** Stable per-role lifecycle exported in the extended DeviceStatus response. */
enum class Lifecycle : std::uint8_t {
    DisabledOrAbsent = 0U,
    Initializing = 1U,
    Live = 2U,
    Degraded = 3U,
};

struct StepResult final {
    bool state_advanced{};
    bool bus_operation{};
    bool shutdown_operation{};
    bool sample_published{};
    std::uint8_t instance_id{0xFFU};
    /** Nonzero after electrical recovery; all shared clients must wait. */
    std::uint64_t bus_settle_until_us{};
};

/**
 * Four-device manager. Every step performs at most one bounded I2C transfer or
 * one bounded recovery operation. It never sleeps, spins, or allocates.
 */
class ArrayDriver final {
  public:
    ArrayDriver(interfaces::II2cBus& bus,
                interfaces::IMonotonicClock& clock,
                interfaces::ISamplePublisher& publisher,
                interfaces::ISensorShutdownBank& shutdown,
                ArrayConfig config = {}) noexcept;

    [[nodiscard]] auto step() noexcept -> StepResult;
    [[nodiscard]] auto step_at(std::uint64_t now_us) noexcept -> StepResult;
    [[nodiscard]] auto ready_at(std::uint64_t now_us) const noexcept -> bool;
    [[nodiscard]] auto next_deadline_us() const noexcept -> std::uint64_t;
    [[nodiscard]] auto state() const noexcept -> ArrayState;
    [[nodiscard]] auto current_instance() const noexcept -> std::uint8_t;
    [[nodiscard]] auto health(std::size_t instance) const noexcept
        -> const DeviceHealth&;
    [[nodiscard]] auto lifecycle(std::size_t instance) const noexcept -> Lifecycle;
    [[nodiscard]] auto configured_address(std::size_t instance) const noexcept
        -> std::uint8_t;
    [[nodiscard]] auto set_sample_interval_us(std::uint32_t interval_us) noexcept
        -> bool;

    /** Called only by the common bus arbiter after another client recovers I2C. */
    void invalidate_after_shared_recovery(std::uint64_t settle_until_us) noexcept;

  private:
    enum class ConfigurePhase : std::uint8_t;
    enum class MeasurePhase : std::uint8_t;

    [[nodiscard]] auto write_register(std::uint8_t address,
                                      std::uint8_t reg,
                                      std::uint8_t value) noexcept
        -> interfaces::I2cStatus;
    [[nodiscard]] auto write_block(std::uint8_t address,
                                   std::uint8_t reg,
                                   const std::uint8_t* data,
                                   std::size_t size) noexcept
        -> interfaces::I2cStatus;
    [[nodiscard]] auto read_block(std::uint8_t address,
                                  std::uint8_t reg,
                                  std::uint8_t* data,
                                  std::size_t size) noexcept
        -> interfaces::I2cStatus;
    [[nodiscard]] auto configure_current(std::uint64_t now_us,
                                         StepResult& result) noexcept -> bool;
    [[nodiscard]] auto prepare_measurement(std::uint64_t now_us,
                                           StepResult& result) noexcept -> bool;
    void select_work(std::uint64_t now_us) noexcept;
    void configuration_complete(std::uint64_t now_us) noexcept;
    void publish_current(std::uint64_t now_us, StepResult& result) noexcept;
    void fail_current(std::uint64_t now_us,
                      DriverError error,
                      interfaces::I2cStatus i2c_status,
                      bool shared_bus_fault) noexcept;
    void isolate_current(std::uint64_t now_us) noexcept;
    void schedule_retry(std::size_t instance, std::uint64_t now_us) noexcept;
    [[nodiscard]] auto valid_config() const noexcept -> bool;

    interfaces::II2cBus& bus_;
    interfaces::IMonotonicClock& clock_;
    interfaces::ISamplePublisher& publisher_;
    interfaces::ISensorShutdownBank& shutdown_;
    ArrayConfig config_{};
    ArrayState state_{ArrayState::Disabled};
    ConfigurePhase configure_phase_{};
    MeasurePhase measure_phase_{};
    std::array<DeviceHealth, kSensorCount> health_{};
    std::array<std::uint32_t, kSensorCount> sequence_{};
    std::array<std::uint64_t, kSensorCount> retry_at_us_{};
    std::array<std::uint64_t, kSensorCount> next_sample_at_us_{};
    std::array<bool, kSensorCount> address_assigned_{};
    std::array<bool, kSensorCount> recovered_since_sample_{};
    std::array<std::uint8_t, 6U> reference_spad_map_{};
    std::array<std::uint8_t, 12U> range_bytes_{};
    RangeReading pending_reading_{};
    std::uint64_t deadline_us_{};
    std::uint64_t operation_timeout_at_us_{};
    std::size_t current_{};
    std::size_t round_robin_cursor_{};
    std::size_t script_index_{};
    std::uint8_t scratch_{};
    // Register 0x91 is factory/device state captured independently during
    // initialization and must be restored to the same device before ranging.
    std::array<std::uint8_t, kSensorCount> stop_variables_{};
    std::uint8_t requested_spad_count_{};
    bool requested_spads_aperture_{};
    DriverError pending_failure_error_{DriverError::None};
    interfaces::I2cStatus pending_failure_i2c_{interfaces::I2cStatus::Ok};
};

}  // namespace shahbaz::sensors::vl53l0x
