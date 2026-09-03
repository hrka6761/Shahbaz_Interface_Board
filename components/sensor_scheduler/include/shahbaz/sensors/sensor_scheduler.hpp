/**
 * @file sensor_scheduler.hpp
 * @brief Cooperative, deadline-driven SHT3x and MS5611 state machines.
 *
 * Every call to a state-machine step performs at most one bounded I2C transfer
 * or one bounded recovery operation. No method sleeps, spins, or allocates.
 */
#pragma once

#include "sensor_ms5611/ms5611_domain.hpp"
#include "sensor_sht30/sht3x_domain.hpp"
#include "sensor_vl53l0x/vl53l0x_array.hpp"
#include "shahbaz/interfaces/i2c_bus.hpp"
#include "shahbaz/interfaces/monotonic_clock.hpp"
#include "shahbaz/interfaces/sample_publisher.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace shahbaz::sensors::scheduler {

/** High-level failure classification retained in per-sensor health. */
enum class DriverError : std::uint8_t {
    None = 0,
    InvalidConfiguration,
    I2cTransfer,
    I2cRecovery,
    DataLength,
    DataCrc,
    SensorStatus,
    InvalidCalibration,
    InvalidAdc,
    Compensation,
    StaleTemperature,
    PublisherBackpressure,
};

/** Fixed-size health counters for diagnostics and fault supervision. */
struct DriverHealth final {
    bool online{};
    /** False means construction rejected the address or typed sensor mode. */
    bool configuration_valid{true};
    bool configuration_clamped{};
    bool sampling_rate_clamped{};
    DriverError last_error{DriverError::None};
    interfaces::I2cStatus last_i2c_status{interfaces::I2cStatus::Ok};
    std::uint32_t consecutive_failures{};
    std::uint32_t total_failures{};
    std::uint32_t i2c_failures{};
    std::uint32_t crc_failures{};
    std::uint32_t recoveries{};
    std::uint32_t recovery_failures{};
    std::uint32_t shared_bus_invalidations{};
    std::uint32_t sensor_status_warnings{};
    std::uint32_t publication_drops{};
};

/** Outcome of one cooperative state-machine invocation. */
struct StepResult final {
    bool state_advanced{};
    bool bus_operation{};
    bool sample_published{};
    /** Nonzero after recovery; blocks every shared-bus client until this time. */
    std::uint64_t bus_settle_until_us{};
};

/** Shared finite retry/recovery policy. */
struct RetryConfig final {
    std::uint32_t initial_backoff_us{10'000U};
    std::uint32_t maximum_backoff_us{100'000U};
    std::uint32_t offline_retry_us{1'000'000U};
    std::uint32_t recovery_timeout_us{5'000U};
    std::uint8_t maximum_consecutive_failures{3U};
};

/** SHT3x timing and rate configuration. */
struct Sht3xConfig final {
    std::uint8_t address{sht3x::kDefaultAddress};
    sht3x::Repeatability repeatability{sht3x::Repeatability::High};
    std::uint32_t transaction_timeout_us{5'000U};
    std::uint32_t power_up_wait_us{2'000U};
    std::uint32_t soft_reset_wait_us{sht3x::kDefaultSoftResetDeadlineUs};
    std::uint32_t conversion_margin_us{2'000U};
    std::uint32_t sample_interval_us{500'000U};
    RetryConfig retry{};
};

/** Observable SHT3x acquisition state. */
enum class Sht3xState : std::uint8_t {
    PowerUpDelay,
    SendSoftReset,
    WaitAfterReset,
    ReadStatus,
    WaitBeforeClearStatus,
    ClearStatus,
    WaitBeforeMeasurement,
    StartMeasurement,
    WaitForMeasurement,
    ReadMeasurement,
    Idle,
    RetryBackoff,
    RecoverBus,
    RecoverySettle,
    Offline,
};

/** Nonblocking SHT3x initialization and single-shot acquisition machine. */
class Sht3xStateMachine final {
  public:
    Sht3xStateMachine(interfaces::II2cBus& bus,
                      interfaces::IMonotonicClock& clock,
                      interfaces::ISamplePublisher& publisher,
                      Sht3xConfig config = {}) noexcept;

    /** Advances at most one state and performs at most one bus operation. */
    [[nodiscard]] auto step() noexcept -> StepResult;
    /** Same operation with an explicit timestamp for shared scheduling/tests. */
    [[nodiscard]] auto step_at(std::uint64_t now_us) noexcept -> StepResult;
    [[nodiscard]] auto ready_at(std::uint64_t now_us) const noexcept -> bool;
    [[nodiscard]] auto next_deadline_us() const noexcept -> std::uint64_t;
    [[nodiscard]] auto state() const noexcept -> Sht3xState;
    [[nodiscard]] auto health() const noexcept -> const DriverHealth&;
    [[nodiscard]] auto has_status() const noexcept -> bool;
    [[nodiscard]] auto last_status() const noexcept -> const sht3x::Status&;
    /** Updates the sampling cadence without reinitializing the sensor. */
    [[nodiscard]] auto set_sample_interval_us(std::uint32_t interval_us) noexcept -> bool;

  private:
    [[nodiscard]] auto write_command(std::uint16_t command) noexcept
        -> interfaces::I2cStatus;
    void fail(std::uint64_t now_us, DriverError error,
              interfaces::I2cStatus i2c_status,
              bool recover_bus) noexcept;
    void restart_after_backoff(std::uint64_t now_us) noexcept;
    void publish_sample(const sht3x::Sample& value,
                        std::uint64_t now_us,
                        StepResult& result) noexcept;
    void invalidate_after_shared_recovery(
        std::uint64_t settle_until_us) noexcept;

    friend class SharedSensorScheduler;

    interfaces::II2cBus& bus_;
    interfaces::IMonotonicClock& clock_;
    interfaces::ISamplePublisher& publisher_;
    Sht3xConfig config_{};
    Sht3xState state_{Sht3xState::PowerUpDelay};
    DriverHealth health_{};
    std::uint64_t deadline_us_{};
    std::uint64_t next_sample_due_us_{};
    std::uint32_t retry_backoff_us_{};
    std::uint32_t sequence_{};
    sht3x::Status last_status_{};
    bool recovery_required_{};
    bool recovered_since_sample_{};
    bool status_valid_{};
    bool initialization_complete_{};
};

/** MS5611 rate, OSR, and timeout configuration. */
struct Ms5611Config final {
    std::uint8_t address{ms5611::kAddressCsbLow};
    ms5611::Osr pressure_osr{ms5611::Osr::Osr4096};
    ms5611::Osr temperature_osr{ms5611::Osr::Osr4096};
    std::uint32_t transaction_timeout_us{5'000U};
    std::uint32_t reset_wait_us{ms5611::kDefaultResetWaitUs};
    std::uint32_t conversion_margin_us{200U};
    std::uint32_t pressure_interval_us{40'000U};
    std::uint32_t temperature_interval_us{250'000U};
    std::uint32_t maximum_temperature_age_us{400'000U};
    RetryConfig retry{};
};

/** Observable MS5611 initialization and conversion state. */
enum class Ms5611State : std::uint8_t {
    SendReset,
    WaitForPromReload,
    ReadProm,
    ValidateProm,
    Idle,
    StartTemperatureD2,
    WaitForTemperatureD2,
    ReadTemperatureD2,
    StartPressureD1,
    WaitForPressureD1,
    ReadPressureD1,
    RetryBackoff,
    RecoverBus,
    RecoverySettle,
    Offline,
};

/** Nonblocking MS5611 reset/PROM/D2/D1 acquisition machine. */
class Ms5611StateMachine final {
  public:
    Ms5611StateMachine(interfaces::II2cBus& bus,
                       interfaces::IMonotonicClock& clock,
                       interfaces::ISamplePublisher& publisher,
                       Ms5611Config config = {}) noexcept;

    /** Advances at most one state and performs at most one bus operation. */
    [[nodiscard]] auto step() noexcept -> StepResult;
    [[nodiscard]] auto step_at(std::uint64_t now_us) noexcept -> StepResult;
    [[nodiscard]] auto ready_at(std::uint64_t now_us) const noexcept -> bool;
    [[nodiscard]] auto next_deadline_us() const noexcept -> std::uint64_t;
    [[nodiscard]] auto state() const noexcept -> Ms5611State;
    [[nodiscard]] auto health() const noexcept -> const DriverHealth&;
    /** Completion timestamp of the most recently acquired valid D1 value. */
    [[nodiscard]] auto last_pressure_timestamp_us() const noexcept
        -> std::uint64_t;
    /** Completion timestamp of the most recently acquired valid D2 value. */
    [[nodiscard]] auto last_temperature_timestamp_us() const noexcept
        -> std::uint64_t;
    /** Updates the pressure publication cadence; D2 cadence remains independent. */
    [[nodiscard]] auto set_pressure_interval_us(std::uint32_t interval_us) noexcept -> bool;

  private:
    [[nodiscard]] auto write_command(std::uint8_t command) noexcept
        -> interfaces::I2cStatus;
    [[nodiscard]] auto read_adc(std::array<std::uint8_t, 3>& bytes) noexcept
        -> interfaces::I2cStatus;
    void fail(std::uint64_t now_us, DriverError error,
              interfaces::I2cStatus i2c_status,
              bool recover_bus) noexcept;
    void restart_after_backoff(std::uint64_t now_us) noexcept;
    void reset_initialization_state() noexcept;
    void publish_sample(const ms5611::CompensatedSample& value,
                        std::uint64_t now_us,
                        StepResult& result) noexcept;
    void invalidate_after_shared_recovery(
        std::uint64_t settle_until_us) noexcept;

    friend class SharedSensorScheduler;

    interfaces::II2cBus& bus_;
    interfaces::IMonotonicClock& clock_;
    interfaces::ISamplePublisher& publisher_;
    Ms5611Config config_{};
    Ms5611State state_{Ms5611State::SendReset};
    DriverHealth health_{};
    ms5611::PromImage prom_{};
    ms5611::Calibration calibration_{};
    std::uint8_t prom_index_{};
    std::uint64_t deadline_us_{};
    std::uint64_t next_pressure_due_us_{};
    std::uint64_t next_temperature_due_us_{};
    std::uint64_t last_pressure_timestamp_us_{};
    std::uint64_t last_temperature_timestamp_us_{};
    std::uint32_t raw_temperature_d2_{};
    std::uint32_t retry_backoff_us_{};
    std::uint32_t sequence_{};
    bool prom_valid_{};
    bool temperature_valid_{};
    bool recovery_required_{};
    bool recovered_since_sample_{};
};

/** Identifies which machine was advanced by a shared scheduler call. */
enum class ScheduledSensor : std::uint8_t {
    None = 0,
    Sht3x,
    Ms5611,
    Vl53l0x,
};

struct SchedulerStepResult final {
    ScheduledSensor sensor{ScheduledSensor::None};
    StepResult step{};
};

/** Fair cooperative arbiter for the two machines sharing one I2C bus. */
class SharedSensorScheduler final {
  public:
    SharedSensorScheduler(interfaces::II2cBus& bus,
                          interfaces::IMonotonicClock& clock,
                          interfaces::ISamplePublisher& publisher,
                          Sht3xConfig sht_config = {},
                          Ms5611Config ms_config = {},
                          interfaces::ISensorShutdownBank* rangefinder_shutdown = nullptr,
                          vl53l0x::ArrayConfig rangefinder_config = {}) noexcept;

    /** Advances at most one sensor and therefore at most one bus operation. */
    [[nodiscard]] auto step() noexcept -> SchedulerStepResult;
    [[nodiscard]] auto next_deadline_us() const noexcept -> std::uint64_t;

    [[nodiscard]] auto sht3x() const noexcept -> const Sht3xStateMachine&;
    [[nodiscard]] auto ms5611() const noexcept -> const Ms5611StateMachine&;
    [[nodiscard]] auto vl53l0x_array() const noexcept
        -> const vl53l0x::ArrayDriver*;
    [[nodiscard]] auto set_sht3x_interval_us(std::uint32_t interval_us) noexcept -> bool;
    [[nodiscard]] auto set_ms5611_interval_us(std::uint32_t interval_us) noexcept -> bool;
    [[nodiscard]] auto set_vl53l0x_interval_us(std::uint32_t interval_us) noexcept -> bool;

  private:
    interfaces::IMonotonicClock& clock_;
    Sht3xStateMachine sht3x_;
    Ms5611StateMachine ms5611_;
    std::optional<vl53l0x::ArrayDriver> vl53l0x_{};
    std::uint8_t round_robin_index_{};
    std::uint64_t bus_settle_until_us_{};
};

}  // namespace shahbaz::sensors::scheduler
