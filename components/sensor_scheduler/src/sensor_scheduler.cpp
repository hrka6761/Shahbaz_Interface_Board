/**
 * @file sensor_scheduler.cpp
 * @brief Allocation-free cooperative sensor acquisition implementation.
 */
#include "shahbaz/sensors/sensor_scheduler.hpp"

#include "shahbaz/domain/measurement.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace shahbaz::sensors::scheduler {
namespace {

constexpr std::uint32_t kShtTemperatureOutOfRange = 1U << 0U;
constexpr std::uint32_t kMsPressureOutOfRange = 1U << 0U;
constexpr std::uint32_t kMsTemperatureOutOfRange = 1U << 1U;
constexpr std::uint32_t kRecoverySettleUs = 1'000U;
constexpr std::uint32_t kMinimumRetryDelayUs = 1'000U;

[[nodiscard]] constexpr auto valid_sht_repeatability(
    const sht3x::Repeatability repeatability) noexcept -> bool {
    switch (repeatability) {
        case sht3x::Repeatability::Low:
        case sht3x::Repeatability::Medium:
        case sht3x::Repeatability::High:
            return true;
    }
    return false;
}

[[nodiscard]] constexpr auto valid_ms_osr(const ms5611::Osr osr) noexcept
    -> bool {
    switch (osr) {
        case ms5611::Osr::Osr256:
        case ms5611::Osr::Osr512:
        case ms5611::Osr::Osr1024:
        case ms5611::Osr::Osr2048:
        case ms5611::Osr::Osr4096:
            return true;
    }
    return false;
}

[[nodiscard]] constexpr auto valid_sht_identity_config(
    const Sht3xConfig& config) noexcept -> bool {
    return (config.address == sht3x::kDefaultAddress ||
            config.address == sht3x::kAlternateAddress) &&
           valid_sht_repeatability(config.repeatability);
}

[[nodiscard]] constexpr auto valid_ms_identity_config(
    const Ms5611Config& config) noexcept -> bool {
    return (config.address == ms5611::kAddressCsbLow ||
            config.address == ms5611::kAddressCsbHigh) &&
           valid_ms_osr(config.pressure_osr) &&
           valid_ms_osr(config.temperature_osr);
}

[[nodiscard]] constexpr auto saturating_add(const std::uint64_t base,
                                            const std::uint64_t delta) noexcept
    -> std::uint64_t {
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    return delta > maximum - base ? maximum : base + delta;
}

void saturating_increment(std::uint32_t& value) noexcept {
    if (value != std::numeric_limits<std::uint32_t>::max()) {
        ++value;
    }
}

[[nodiscard]] constexpr auto advance_periodic_deadline(
    const std::uint64_t prior_deadline, const std::uint64_t now_us,
    const std::uint32_t interval_us) noexcept -> std::uint64_t {
    if (prior_deadline == 0U || prior_deadline > now_us) {
        return prior_deadline == 0U
                   ? saturating_add(now_us, interval_us)
                   : prior_deadline;
    }
    const auto periods =
        ((now_us - prior_deadline) / interval_us) + 1U;
    if (periods > std::numeric_limits<std::uint64_t>::max() / interval_us) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return saturating_add(prior_deadline, periods * interval_us);
}

[[nodiscard]] constexpr auto next_backoff(const std::uint32_t current,
                                          const RetryConfig& config) noexcept
    -> std::uint32_t {
    if (current < config.initial_backoff_us) {
        return std::min(config.initial_backoff_us,
                        config.maximum_backoff_us);
    }
    if (current >= config.maximum_backoff_us ||
        current > std::numeric_limits<std::uint32_t>::max() / 2U) {
        return config.maximum_backoff_us;
    }
    return std::min(static_cast<std::uint32_t>(current * 2U),
                    config.maximum_backoff_us);
}

[[nodiscard]] constexpr auto normalize_retry(RetryConfig config) noexcept
    -> RetryConfig {
    config.initial_backoff_us =
        std::max(config.initial_backoff_us, kMinimumRetryDelayUs);
    if (config.maximum_backoff_us < config.initial_backoff_us) {
        config.maximum_backoff_us = config.initial_backoff_us;
    }
    config.offline_retry_us =
        std::max(config.offline_retry_us, config.initial_backoff_us);
    if (config.maximum_consecutive_failures == 0U) {
        config.maximum_consecutive_failures = 1U;
    }
    if (config.recovery_timeout_us == 0U) {
        config.recovery_timeout_us = 1U;
    }
    return config;
}

[[nodiscard]] constexpr auto normalize_sht_config(Sht3xConfig config) noexcept
    -> Sht3xConfig {
    config.retry = normalize_retry(config.retry);
    config.power_up_wait_us =
        std::max(config.power_up_wait_us,
                 sht3x::kDefaultSoftResetDeadlineUs);
    config.soft_reset_wait_us =
        std::max(config.soft_reset_wait_us,
                 sht3x::kDefaultSoftResetDeadlineUs);
    if (config.transaction_timeout_us == 0U) {
        config.transaction_timeout_us = 1U;
    }
    const auto timing = sht3x::measurement_profile(config.repeatability);
    const auto minimum_sample_interval = static_cast<std::uint32_t>(std::min(
        static_cast<std::uint64_t>(timing.maximum_duration_us) +
            config.conversion_margin_us,
        static_cast<std::uint64_t>(
            std::numeric_limits<std::uint32_t>::max())));
    config.sample_interval_us = std::max(
        config.sample_interval_us,
        std::max(minimum_sample_interval, std::uint32_t{1U}));
    return config;
}

[[nodiscard]] constexpr auto normalize_ms_config(Ms5611Config config) noexcept
    -> Ms5611Config {
    config.retry = normalize_retry(config.retry);
    config.reset_wait_us =
        std::max(config.reset_wait_us, ms5611::kMinimumResetWaitUs);
    if (config.transaction_timeout_us == 0U) {
        config.transaction_timeout_us = 1U;
    }
    const auto pressure_timing = ms5611::conversion_profile(
        ms5611::ConversionKind::PressureD1, config.pressure_osr);
    const auto temperature_timing = ms5611::conversion_profile(
        ms5611::ConversionKind::TemperatureD2, config.temperature_osr);
    const auto minimum_pressure_interval = static_cast<std::uint32_t>(std::min(
        static_cast<std::uint64_t>(pressure_timing.maximum_duration_us) +
            config.conversion_margin_us,
        static_cast<std::uint64_t>(
            std::numeric_limits<std::uint32_t>::max())));
    const auto minimum_temperature_interval =
        static_cast<std::uint32_t>(std::min(
            static_cast<std::uint64_t>(temperature_timing.maximum_duration_us) +
                config.conversion_margin_us,
            static_cast<std::uint64_t>(
                std::numeric_limits<std::uint32_t>::max())));
    config.pressure_interval_us = std::max(
        config.pressure_interval_us,
        std::max(minimum_pressure_interval, std::uint32_t{1U}));
    config.temperature_interval_us = std::max(
        config.temperature_interval_us,
        std::max(minimum_temperature_interval, std::uint32_t{1U}));
    const auto minimum_temperature_age = static_cast<std::uint32_t>(std::min(
        static_cast<std::uint64_t>(config.temperature_interval_us) +
            static_cast<std::uint64_t>(pressure_timing.maximum_duration_us) +
            config.conversion_margin_us,
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())));
    config.maximum_temperature_age_us =
        std::max(config.maximum_temperature_age_us, minimum_temperature_age);
    return config;
}

[[nodiscard]] constexpr auto valid_sample_flags(const bool calibrated) noexcept
    -> domain::ValidityFlag {
    auto flags = domain::ValidityFlag::TransportValid |
                 domain::ValidityFlag::CrcValid |
                 domain::ValidityFlag::TimingValid;
    if (calibrated) {
        flags = flags | domain::ValidityFlag::CalibrationValid;
    }
    return flags;
}

[[nodiscard]] constexpr auto fresh_quality(const bool recovered,
                                           const bool rate_limited) noexcept
    -> domain::QualityFlag {
    auto quality = domain::QualityFlag::Fresh;
    if (recovered) {
        quality = quality | domain::QualityFlag::RecoveredAfterError;
    }
    if (rate_limited) {
        quality = quality | domain::QualityFlag::RateLimited;
    }
    return quality;
}

}  // namespace

Sht3xStateMachine::Sht3xStateMachine(
    interfaces::II2cBus& bus, interfaces::IMonotonicClock& clock,
    interfaces::ISamplePublisher& publisher, Sht3xConfig config) noexcept
    : bus_(bus),
      clock_(clock),
      publisher_(publisher),
      config_(normalize_sht_config(config)),
      deadline_us_(saturating_add(clock.now_us(), config_.power_up_wait_us)),
      retry_backoff_us_(config_.retry.initial_backoff_us) {
    health_.configuration_valid = valid_sht_identity_config(config);
    health_.configuration_clamped =
        config_.transaction_timeout_us != config.transaction_timeout_us ||
        config_.power_up_wait_us != config.power_up_wait_us ||
        config_.soft_reset_wait_us != config.soft_reset_wait_us ||
        config_.sample_interval_us != config.sample_interval_us ||
        config_.retry.initial_backoff_us != config.retry.initial_backoff_us ||
        config_.retry.maximum_backoff_us != config.retry.maximum_backoff_us ||
        config_.retry.offline_retry_us != config.retry.offline_retry_us ||
        config_.retry.recovery_timeout_us != config.retry.recovery_timeout_us ||
        config_.retry.maximum_consecutive_failures !=
            config.retry.maximum_consecutive_failures;
    health_.sampling_rate_clamped =
        config_.sample_interval_us != config.sample_interval_us;
    if (!health_.configuration_valid) {
        health_.last_error = DriverError::InvalidConfiguration;
        state_ = Sht3xState::Offline;
        deadline_us_ = std::numeric_limits<std::uint64_t>::max();
    }
}

auto Sht3xStateMachine::write_command(const std::uint16_t command) noexcept
    -> interfaces::I2cStatus {
    const std::array<std::uint8_t, 2> bytes{
        static_cast<std::uint8_t>(command >> 8U),
        static_cast<std::uint8_t>(command & 0x00FFU),
    };
    const interfaces::I2cTransaction transaction{
        config_.address,
        {bytes.data(), bytes.size()},
        {},
        config_.transaction_timeout_us,
    };
    return bus_.transfer(transaction);
}

void Sht3xStateMachine::fail(const std::uint64_t now_us,
                             const DriverError error,
                             const interfaces::I2cStatus i2c_status,
                             const bool recover_bus) noexcept {
    health_.online = false;
    health_.last_error = error;
    health_.last_i2c_status = i2c_status;
    saturating_increment(health_.total_failures);
    saturating_increment(health_.consecutive_failures);
    if (error == DriverError::I2cTransfer) {
        saturating_increment(health_.i2c_failures);
    } else if (error == DriverError::DataCrc) {
        saturating_increment(health_.crc_failures);
    }
    recovered_since_sample_ = true;
    recovery_required_ = recover_bus;
    initialization_complete_ = false;

    if (health_.consecutive_failures >=
        static_cast<std::uint32_t>(config_.retry.maximum_consecutive_failures)) {
        state_ = Sht3xState::Offline;
        deadline_us_ = saturating_add(now_us, config_.retry.offline_retry_us);
        return;
    }

    retry_backoff_us_ =
        health_.consecutive_failures == 1U
            ? config_.retry.initial_backoff_us
            : next_backoff(retry_backoff_us_, config_.retry);
    state_ = Sht3xState::RetryBackoff;
    deadline_us_ = saturating_add(now_us, retry_backoff_us_);
}

void Sht3xStateMachine::restart_after_backoff(
    const std::uint64_t now_us) noexcept {
    recovery_required_ = false;
    state_ = Sht3xState::SendSoftReset;
    deadline_us_ = now_us;
}

void Sht3xStateMachine::invalidate_after_shared_recovery(
    const std::uint64_t settle_until_us) noexcept {
    if (!health_.configuration_valid) {
        return;
    }
    saturating_increment(health_.shared_bus_invalidations);
    health_.online = false;
    health_.last_error = DriverError::I2cRecovery;
    recovered_since_sample_ = true;
    recovery_required_ = false;
    status_valid_ = false;
    initialization_complete_ = false;
    if (state_ != Sht3xState::Offline) {
        state_ = Sht3xState::RecoverySettle;
        deadline_us_ = settle_until_us;
    }
}

void Sht3xStateMachine::publish_sample(const sht3x::Sample& value,
                                       const std::uint64_t now_us,
                                       StepResult& result) noexcept {
    const bool plausible = !value.temperature_outside_specified_range &&
                           value.relative_humidity_milli_percent <= 100'000U;
    auto validity = valid_sample_flags(false);
    if (plausible) {
        validity = validity | domain::ValidityFlag::PlausibilityValid;
    }

    domain::SensorSample sample{};
    sample.sensor_id = domain::SensorId::Sht3x;
    sample.instance_id = 0U;
    sample.sequence = sequence_++;
    sample.monotonic_timestamp_us = now_us;
    sample.validity = validity;
    sample.quality = fresh_quality(recovered_since_sample_,
                                   health_.sampling_rate_clamped);
    sample.health_flags = value.temperature_outside_specified_range
                              ? kShtTemperatureOutOfRange
                              : 0U;
    sample.field_count = 2U;
    sample.fields[0] = domain::make_signed_field(
        domain::FieldId::AmbientTemperatureMilliCelsius,
        value.ambient_temperature_milli_celsius);
    sample.fields[1] = domain::make_unsigned_field(
        domain::FieldId::RelativeHumidityMilliPercent,
        value.relative_humidity_milli_percent);

    if (publisher_.publish(sample)) {
        result.sample_published = true;
        recovered_since_sample_ = false;
        health_.last_error = DriverError::None;
    } else {
        saturating_increment(health_.publication_drops);
        health_.last_error = DriverError::PublisherBackpressure;
    }
    health_.online = true;
    health_.last_i2c_status = interfaces::I2cStatus::Ok;
    health_.consecutive_failures = 0U;
    retry_backoff_us_ = config_.retry.initial_backoff_us;
    initialization_complete_ = true;
}

auto Sht3xStateMachine::ready_at(const std::uint64_t now_us) const noexcept
    -> bool {
    if (!health_.configuration_valid) {
        return false;
    }
    switch (state_) {
        case Sht3xState::PowerUpDelay:
        case Sht3xState::WaitAfterReset:
        case Sht3xState::WaitBeforeClearStatus:
        case Sht3xState::WaitBeforeMeasurement:
        case Sht3xState::WaitForMeasurement:
        case Sht3xState::RetryBackoff:
        case Sht3xState::RecoverySettle:
        case Sht3xState::Offline:
        case Sht3xState::Idle:
            return now_us >= deadline_us_;
        case Sht3xState::SendSoftReset:
        case Sht3xState::ReadStatus:
        case Sht3xState::ClearStatus:
        case Sht3xState::StartMeasurement:
        case Sht3xState::ReadMeasurement:
        case Sht3xState::RecoverBus:
            return true;
    }
    return false;
}

auto Sht3xStateMachine::next_deadline_us() const noexcept -> std::uint64_t {
    if (!health_.configuration_valid) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return deadline_us_;
}

auto Sht3xStateMachine::state() const noexcept -> Sht3xState {
    return state_;
}

auto Sht3xStateMachine::health() const noexcept -> const DriverHealth& {
    return health_;
}

auto Sht3xStateMachine::has_status() const noexcept -> bool {
    return status_valid_;
}

auto Sht3xStateMachine::last_status() const noexcept
    -> const sht3x::Status& {
    return last_status_;
}

auto Sht3xStateMachine::set_sample_interval_us(const std::uint32_t interval_us) noexcept
    -> bool {
    const auto timing = sht3x::measurement_profile(config_.repeatability);
    const auto minimum = static_cast<std::uint64_t>(timing.maximum_duration_us) +
                         config_.conversion_margin_us;
    if (interval_us == 0U || static_cast<std::uint64_t>(interval_us) < minimum) {
        return false;
    }
    config_.sample_interval_us = interval_us;
    next_sample_due_us_ = saturating_add(clock_.now_us(), interval_us);
    if (state_ == Sht3xState::Idle) {
        deadline_us_ = next_sample_due_us_;
    }
    return true;
}

auto Sht3xStateMachine::step() noexcept -> StepResult {
    return step_at(clock_.now_us());
}

auto Sht3xStateMachine::step_at(const std::uint64_t now_us) noexcept
    -> StepResult {
    StepResult result{};
    if (!ready_at(now_us)) {
        return result;
    }
    result.state_advanced = true;

    switch (state_) {
        case Sht3xState::PowerUpDelay:
            state_ = Sht3xState::SendSoftReset;
            deadline_us_ = now_us;
            break;

        case Sht3xState::SendSoftReset: {
            result.bus_operation = true;
            const auto status = write_command(sht3x::kSoftResetCommand);
            const auto completed_us = std::max(now_us, clock_.now_us());
            if (status != interfaces::I2cStatus::Ok) {
                fail(completed_us, DriverError::I2cTransfer, status, true);
                break;
            }
            health_.last_i2c_status = status;
            status_valid_ = false;
            state_ = Sht3xState::WaitAfterReset;
            deadline_us_ =
                saturating_add(completed_us, config_.soft_reset_wait_us);
            break;
        }

        case Sht3xState::WaitAfterReset:
            state_ = Sht3xState::ReadStatus;
            deadline_us_ = now_us;
            break;

        case Sht3xState::ReadStatus: {
            const std::array<std::uint8_t, 2> command{0xF3U, 0x2DU};
            std::array<std::uint8_t, 3> response{};
            const interfaces::I2cTransaction transaction{
                config_.address,
                {command.data(), command.size()},
                {response.data(), response.size()},
                config_.transaction_timeout_us,
                interfaces::I2cWriteReadBoundary::StopThenStart,
            };
            result.bus_operation = true;
            const auto status = bus_.transfer(transaction);
            const auto completed_us = std::max(now_us, clock_.now_us());
            if (status != interfaces::I2cStatus::Ok) {
                fail(completed_us, DriverError::I2cTransfer, status, true);
                break;
            }
            const auto parsed = sht3x::parse_status(response.data(),
                                                    response.size());
            if (!parsed.ok()) {
                const auto error = parsed.error == sht3x::Error::InvalidLength
                                       ? DriverError::DataLength
                                       : DriverError::DataCrc;
                const bool repeated_crc =
                    error == DriverError::DataCrc &&
                    health_.consecutive_failures != 0U;
                fail(completed_us, error, interfaces::I2cStatus::Ok,
                     repeated_crc);
                break;
            }
            last_status_ = parsed.value;
            status_valid_ = true;
            if (parsed.value.alert_pending || parsed.value.heater_enabled ||
                parsed.value.relative_humidity_tracking_alert ||
                parsed.value.temperature_tracking_alert ||
                parsed.value.last_command_failed ||
                parsed.value.last_write_checksum_failed) {
                saturating_increment(health_.sensor_status_warnings);
                health_.last_error = DriverError::SensorStatus;
            }
            if (parsed.value.heater_enabled) {
                fail(completed_us, DriverError::SensorStatus,
                     interfaces::I2cStatus::Ok, false);
                break;
            }
            health_.last_i2c_status = status;
            state_ = Sht3xState::WaitBeforeClearStatus;
            deadline_us_ = saturating_add(
                completed_us, sht3x::kMinimumCommandSpacingUs);
            break;
        }

        case Sht3xState::WaitBeforeClearStatus:
            state_ = Sht3xState::ClearStatus;
            deadline_us_ = now_us;
            break;

        case Sht3xState::ClearStatus: {
            result.bus_operation = true;
            const auto status = write_command(sht3x::kClearStatusCommand);
            const auto completed_us = std::max(now_us, clock_.now_us());
            if (status != interfaces::I2cStatus::Ok) {
                fail(completed_us, DriverError::I2cTransfer, status, true);
                break;
            }
            health_.last_i2c_status = status;
            state_ = Sht3xState::WaitBeforeMeasurement;
            deadline_us_ = saturating_add(
                completed_us, sht3x::kMinimumCommandSpacingUs);
            break;
        }

        case Sht3xState::WaitBeforeMeasurement:
            state_ = Sht3xState::StartMeasurement;
            deadline_us_ = now_us;
            break;

        case Sht3xState::StartMeasurement: {
            result.bus_operation = true;
            const auto profile = sht3x::measurement_profile(
                initialization_complete_ ? config_.repeatability
                                         : sht3x::Repeatability::High);
            const auto status = write_command(profile.command);
            const auto completed_us = std::max(now_us, clock_.now_us());
            if (status != interfaces::I2cStatus::Ok) {
                fail(completed_us, DriverError::I2cTransfer, status, true);
                break;
            }
            health_.last_i2c_status = status;
            next_sample_due_us_ = advance_periodic_deadline(
                next_sample_due_us_, completed_us,
                config_.sample_interval_us);
            state_ = Sht3xState::WaitForMeasurement;
            deadline_us_ = saturating_add(
                completed_us,
                static_cast<std::uint64_t>(profile.maximum_duration_us) +
                    config_.conversion_margin_us);
            break;
        }

        case Sht3xState::WaitForMeasurement:
            state_ = Sht3xState::ReadMeasurement;
            deadline_us_ = now_us;
            break;

        case Sht3xState::ReadMeasurement: {
            std::array<std::uint8_t, 6> response{};
            const interfaces::I2cTransaction transaction{
                config_.address,
                {},
                {response.data(), response.size()},
                config_.transaction_timeout_us,
            };
            result.bus_operation = true;
            const auto status = bus_.transfer(transaction);
            const auto completed_us = std::max(now_us, clock_.now_us());
            if (status != interfaces::I2cStatus::Ok) {
                fail(completed_us, DriverError::I2cTransfer, status, true);
                break;
            }
            const auto parsed = sht3x::parse_measurement(response.data(),
                                                         response.size());
            if (!parsed.ok()) {
                const auto error = parsed.error == sht3x::Error::InvalidLength
                                       ? DriverError::DataLength
                                       : DriverError::DataCrc;
                const bool repeated_crc =
                    error == DriverError::DataCrc &&
                    health_.consecutive_failures != 0U;
                fail(completed_us, error, interfaces::I2cStatus::Ok,
                     repeated_crc);
                break;
            }
            publish_sample(parsed.value, completed_us, result);
            state_ = Sht3xState::Idle;
            deadline_us_ = next_sample_due_us_;
            break;
        }

        case Sht3xState::Idle:
            state_ = Sht3xState::StartMeasurement;
            deadline_us_ = now_us;
            break;

        case Sht3xState::RetryBackoff:
            if (recovery_required_) {
                state_ = Sht3xState::RecoverBus;
                deadline_us_ = now_us;
            } else {
                restart_after_backoff(now_us);
            }
            break;

        case Sht3xState::RecoverBus: {
            result.bus_operation = true;
            const auto status =
                bus_.recover(config_.retry.recovery_timeout_us);
            const auto completed_us = std::max(now_us, clock_.now_us());
            result.bus_settle_until_us =
                saturating_add(completed_us, kRecoverySettleUs);
            if (status != interfaces::I2cStatus::Ok) {
                saturating_increment(health_.recovery_failures);
                fail(completed_us, DriverError::I2cRecovery, status, false);
                break;
            }
            saturating_increment(health_.recoveries);
            health_.last_i2c_status = status;
            recovery_required_ = false;
            state_ = Sht3xState::RecoverySettle;
            deadline_us_ = result.bus_settle_until_us;
            break;
        }

        case Sht3xState::RecoverySettle:
            restart_after_backoff(now_us);
            break;

        case Sht3xState::Offline:
            health_.consecutive_failures = 0U;
            retry_backoff_us_ = config_.retry.initial_backoff_us;
            restart_after_backoff(now_us);
            break;
    }

    return result;
}

Ms5611StateMachine::Ms5611StateMachine(
    interfaces::II2cBus& bus, interfaces::IMonotonicClock& clock,
    interfaces::ISamplePublisher& publisher, Ms5611Config config) noexcept
    : bus_(bus),
      clock_(clock),
      publisher_(publisher),
      config_(normalize_ms_config(config)),
      deadline_us_(clock.now_us()),
      next_pressure_due_us_(clock.now_us()),
      next_temperature_due_us_(clock.now_us()),
      retry_backoff_us_(config_.retry.initial_backoff_us) {
    health_.configuration_valid = valid_ms_identity_config(config);
    health_.configuration_clamped =
        config_.transaction_timeout_us != config.transaction_timeout_us ||
        config_.reset_wait_us != config.reset_wait_us ||
        config_.pressure_interval_us != config.pressure_interval_us ||
        config_.temperature_interval_us != config.temperature_interval_us ||
        config_.maximum_temperature_age_us !=
            config.maximum_temperature_age_us ||
        config_.retry.initial_backoff_us != config.retry.initial_backoff_us ||
        config_.retry.maximum_backoff_us != config.retry.maximum_backoff_us ||
        config_.retry.offline_retry_us != config.retry.offline_retry_us ||
        config_.retry.recovery_timeout_us != config.retry.recovery_timeout_us ||
        config_.retry.maximum_consecutive_failures !=
            config.retry.maximum_consecutive_failures;
    health_.sampling_rate_clamped =
        config_.pressure_interval_us != config.pressure_interval_us ||
        config_.temperature_interval_us != config.temperature_interval_us;
    if (!health_.configuration_valid) {
        health_.last_error = DriverError::InvalidConfiguration;
        state_ = Ms5611State::Offline;
        deadline_us_ = std::numeric_limits<std::uint64_t>::max();
    }
}

auto Ms5611StateMachine::write_command(const std::uint8_t command) noexcept
    -> interfaces::I2cStatus {
    const std::array<std::uint8_t, 1> bytes{command};
    const interfaces::I2cTransaction transaction{
        config_.address,
        {bytes.data(), bytes.size()},
        {},
        config_.transaction_timeout_us,
    };
    return bus_.transfer(transaction);
}

auto Ms5611StateMachine::read_adc(
    std::array<std::uint8_t, 3>& bytes) noexcept -> interfaces::I2cStatus {
    const std::array<std::uint8_t, 1> command{ms5611::kAdcReadCommand};
    const interfaces::I2cTransaction transaction{
        config_.address,
        {command.data(), command.size()},
        {bytes.data(), bytes.size()},
        config_.transaction_timeout_us,
        interfaces::I2cWriteReadBoundary::StopThenStart,
    };
    return bus_.transfer(transaction);
}

void Ms5611StateMachine::reset_initialization_state() noexcept {
    prom_.fill(0U);
    calibration_ = {};
    prom_index_ = 0U;
    prom_valid_ = false;
    temperature_valid_ = false;
    raw_temperature_d2_ = 0U;
    last_pressure_timestamp_us_ = 0U;
    last_temperature_timestamp_us_ = 0U;
}

void Ms5611StateMachine::fail(const std::uint64_t now_us,
                              const DriverError error,
                              const interfaces::I2cStatus i2c_status,
                              const bool recover_bus) noexcept {
    health_.online = false;
    health_.last_error = error;
    health_.last_i2c_status = i2c_status;
    saturating_increment(health_.total_failures);
    saturating_increment(health_.consecutive_failures);
    if (error == DriverError::I2cTransfer) {
        saturating_increment(health_.i2c_failures);
    } else if (error == DriverError::DataCrc) {
        saturating_increment(health_.crc_failures);
    }
    recovered_since_sample_ = true;
    recovery_required_ = recover_bus;
    reset_initialization_state();

    if (health_.consecutive_failures >=
        static_cast<std::uint32_t>(config_.retry.maximum_consecutive_failures)) {
        state_ = Ms5611State::Offline;
        deadline_us_ = saturating_add(now_us, config_.retry.offline_retry_us);
        return;
    }

    retry_backoff_us_ =
        health_.consecutive_failures == 1U
            ? config_.retry.initial_backoff_us
            : next_backoff(retry_backoff_us_, config_.retry);
    state_ = Ms5611State::RetryBackoff;
    deadline_us_ = saturating_add(now_us, retry_backoff_us_);
}

void Ms5611StateMachine::restart_after_backoff(
    const std::uint64_t now_us) noexcept {
    recovery_required_ = false;
    reset_initialization_state();
    state_ = Ms5611State::SendReset;
    deadline_us_ = now_us;
}

void Ms5611StateMachine::invalidate_after_shared_recovery(
    const std::uint64_t settle_until_us) noexcept {
    if (!health_.configuration_valid) {
        return;
    }
    saturating_increment(health_.shared_bus_invalidations);
    health_.online = false;
    health_.last_error = DriverError::I2cRecovery;
    recovered_since_sample_ = true;
    recovery_required_ = false;
    reset_initialization_state();
    if (state_ != Ms5611State::Offline) {
        state_ = Ms5611State::RecoverySettle;
        deadline_us_ = settle_until_us;
    }
}

void Ms5611StateMachine::publish_sample(
    const ms5611::CompensatedSample& value, const std::uint64_t now_us,
    StepResult& result) noexcept {
    const bool pressure_plausible =
        value.pressure_pascal >= 1'000 && value.pressure_pascal <= 120'000;
    const bool temperature_plausible =
        value.internal_temperature_milli_celsius >= -40'000 &&
        value.internal_temperature_milli_celsius <= 85'000;
    auto validity = valid_sample_flags(true);
    if (pressure_plausible && temperature_plausible) {
        validity = validity | domain::ValidityFlag::PlausibilityValid;
    }

    std::uint32_t health_flags = 0U;
    if (!pressure_plausible) {
        health_flags |= kMsPressureOutOfRange;
    }
    if (!temperature_plausible) {
        health_flags |= kMsTemperatureOutOfRange;
    }

    domain::SensorSample sample{};
    sample.sensor_id = domain::SensorId::Ms5611;
    sample.instance_id = 0U;
    sample.sequence = sequence_++;
    sample.monotonic_timestamp_us = now_us;
    sample.validity = validity;
    sample.quality = fresh_quality(recovered_since_sample_,
                                   health_.sampling_rate_clamped);
    sample.health_flags = health_flags;
    sample.field_count = 2U;
    sample.fields[0] = domain::make_signed_field(
        domain::FieldId::CompensatedPressurePascal, value.pressure_pascal);
    sample.fields[1] = domain::make_signed_field(
        domain::FieldId::InternalTemperatureMilliCelsius,
        value.internal_temperature_milli_celsius);

    if (publisher_.publish(sample)) {
        result.sample_published = true;
        recovered_since_sample_ = false;
        health_.last_error = DriverError::None;
    } else {
        saturating_increment(health_.publication_drops);
        health_.last_error = DriverError::PublisherBackpressure;
    }
    health_.online = true;
    health_.last_i2c_status = interfaces::I2cStatus::Ok;
    health_.consecutive_failures = 0U;
    retry_backoff_us_ = config_.retry.initial_backoff_us;
}

auto Ms5611StateMachine::ready_at(const std::uint64_t now_us) const noexcept
    -> bool {
    if (!health_.configuration_valid) {
        return false;
    }
    switch (state_) {
        case Ms5611State::WaitForPromReload:
        case Ms5611State::WaitForTemperatureD2:
        case Ms5611State::WaitForPressureD1:
        case Ms5611State::RetryBackoff:
        case Ms5611State::RecoverySettle:
        case Ms5611State::Offline:
            return now_us >= deadline_us_;
        case Ms5611State::Idle: {
            if (!temperature_valid_) {
                return true;
            }
            const auto temperature_age =
                now_us >= last_temperature_timestamp_us_
                    ? now_us - last_temperature_timestamp_us_
                    : 0U;
            return now_us >= next_temperature_due_us_ ||
                   temperature_age > config_.maximum_temperature_age_us ||
                   now_us >= next_pressure_due_us_;
        }
        case Ms5611State::SendReset:
        case Ms5611State::ReadProm:
        case Ms5611State::ValidateProm:
        case Ms5611State::StartTemperatureD2:
        case Ms5611State::ReadTemperatureD2:
        case Ms5611State::StartPressureD1:
        case Ms5611State::ReadPressureD1:
        case Ms5611State::RecoverBus:
            return true;
    }
    return false;
}

auto Ms5611StateMachine::next_deadline_us() const noexcept -> std::uint64_t {
    if (!health_.configuration_valid) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    if (state_ != Ms5611State::Idle) {
        return deadline_us_;
    }
    if (!temperature_valid_) {
        return deadline_us_;
    }
    const auto maximum_age_deadline = saturating_add(
        last_temperature_timestamp_us_, config_.maximum_temperature_age_us);
    return std::min(next_pressure_due_us_,
                    std::min(next_temperature_due_us_, maximum_age_deadline));
}

auto Ms5611StateMachine::state() const noexcept -> Ms5611State {
    return state_;
}

auto Ms5611StateMachine::health() const noexcept -> const DriverHealth& {
    return health_;
}

auto Ms5611StateMachine::last_pressure_timestamp_us() const noexcept
    -> std::uint64_t {
    return last_pressure_timestamp_us_;
}

auto Ms5611StateMachine::last_temperature_timestamp_us() const noexcept
    -> std::uint64_t {
    return last_temperature_timestamp_us_;
}

auto Ms5611StateMachine::set_pressure_interval_us(const std::uint32_t interval_us) noexcept
    -> bool {
    const auto timing = ms5611::conversion_profile(
        ms5611::ConversionKind::PressureD1, config_.pressure_osr);
    const auto minimum = static_cast<std::uint64_t>(timing.maximum_duration_us) +
                         config_.conversion_margin_us;
    if (interval_us == 0U || static_cast<std::uint64_t>(interval_us) < minimum) {
        return false;
    }
    config_.pressure_interval_us = interval_us;
    next_pressure_due_us_ = saturating_add(clock_.now_us(), interval_us);
    if (state_ == Ms5611State::Idle) {
        deadline_us_ = next_deadline_us();
    }
    return true;
}

auto Ms5611StateMachine::step() noexcept -> StepResult {
    return step_at(clock_.now_us());
}

auto Ms5611StateMachine::step_at(const std::uint64_t now_us) noexcept
    -> StepResult {
    StepResult result{};
    if (!ready_at(now_us)) {
        return result;
    }
    result.state_advanced = true;

    switch (state_) {
        case Ms5611State::SendReset: {
            result.bus_operation = true;
            const auto status = write_command(ms5611::kResetCommand);
            const auto completed_us = std::max(now_us, clock_.now_us());
            if (status != interfaces::I2cStatus::Ok) {
                fail(completed_us, DriverError::I2cTransfer, status, true);
                break;
            }
            health_.last_i2c_status = status;
            state_ = Ms5611State::WaitForPromReload;
            deadline_us_ = saturating_add(completed_us, config_.reset_wait_us);
            break;
        }

        case Ms5611State::WaitForPromReload:
            state_ = Ms5611State::ReadProm;
            deadline_us_ = now_us;
            break;

        case Ms5611State::ReadProm: {
            const std::array<std::uint8_t, 1> command{
                ms5611::prom_read_command(prom_index_)};
            std::array<std::uint8_t, 2> response{};
            const interfaces::I2cTransaction transaction{
                config_.address,
                {command.data(), command.size()},
                {response.data(), response.size()},
                config_.transaction_timeout_us,
                interfaces::I2cWriteReadBoundary::StopThenStart,
            };
            result.bus_operation = true;
            const auto status = bus_.transfer(transaction);
            const auto completed_us = std::max(now_us, clock_.now_us());
            if (status != interfaces::I2cStatus::Ok) {
                fail(completed_us, DriverError::I2cTransfer, status, true);
                break;
            }
            const auto decoded = ms5611::decode_prom_word(response.data(),
                                                           response.size());
            if (!decoded.ok()) {
                fail(completed_us, DriverError::DataLength,
                     interfaces::I2cStatus::Ok, false);
                break;
            }
            prom_[prom_index_] = decoded.value;
            ++prom_index_;
            health_.last_i2c_status = status;
            if (prom_index_ >= prom_.size()) {
                state_ = Ms5611State::ValidateProm;
            }
            deadline_us_ = completed_us;
            break;
        }

        case Ms5611State::ValidateProm: {
            const auto validation = ms5611::validate_prom(prom_);
            if (!validation.ok()) {
                const auto error = validation.error == ms5611::Error::PromCrcMismatch
                                       ? DriverError::DataCrc
                                       : DriverError::InvalidCalibration;
                const bool repeated_crc =
                    error == DriverError::DataCrc &&
                    health_.consecutive_failures != 0U;
                fail(now_us, error, interfaces::I2cStatus::Ok, repeated_crc);
                break;
            }
            calibration_ = validation.calibration;
            prom_valid_ = true;
            temperature_valid_ = false;
            next_pressure_due_us_ = now_us;
            next_temperature_due_us_ = now_us;
            state_ = Ms5611State::Idle;
            deadline_us_ = now_us;
            break;
        }

        case Ms5611State::Idle: {
            const auto temperature_age =
                now_us >= last_temperature_timestamp_us_
                    ? now_us - last_temperature_timestamp_us_
                    : 0U;
            if (!temperature_valid_ ||
                temperature_age > config_.maximum_temperature_age_us) {
                state_ = Ms5611State::StartTemperatureD2;
            } else if (now_us >= next_pressure_due_us_) {
                // A fresh D2 is sufficient for one pressure conversion. Give a
                // due D1 priority over refreshing D2 again; otherwise a minimum
                // D2 interval can starve pressure forever under cooperative
                // scheduling overhead.
                state_ = Ms5611State::StartPressureD1;
            } else if (now_us >= next_temperature_due_us_) {
                state_ = Ms5611State::StartTemperatureD2;
            } else {
                state_ = Ms5611State::StartPressureD1;
            }
            deadline_us_ = now_us;
            break;
        }

        case Ms5611State::StartTemperatureD2: {
            result.bus_operation = true;
            const auto profile = ms5611::conversion_profile(
                ms5611::ConversionKind::TemperatureD2,
                config_.temperature_osr);
            const auto status = write_command(profile.command);
            const auto completed_us = std::max(now_us, clock_.now_us());
            if (status != interfaces::I2cStatus::Ok) {
                fail(completed_us, DriverError::I2cTransfer, status, true);
                break;
            }
            health_.last_i2c_status = status;
            next_temperature_due_us_ = advance_periodic_deadline(
                next_temperature_due_us_, completed_us,
                config_.temperature_interval_us);
            state_ = Ms5611State::WaitForTemperatureD2;
            deadline_us_ = saturating_add(
                completed_us,
                static_cast<std::uint64_t>(profile.maximum_duration_us) +
                    config_.conversion_margin_us);
            break;
        }

        case Ms5611State::WaitForTemperatureD2:
            state_ = Ms5611State::ReadTemperatureD2;
            deadline_us_ = now_us;
            break;

        case Ms5611State::ReadTemperatureD2: {
            std::array<std::uint8_t, 3> response{};
            result.bus_operation = true;
            const auto status = read_adc(response);
            const auto completed_us = std::max(now_us, clock_.now_us());
            if (status != interfaces::I2cStatus::Ok) {
                fail(completed_us, DriverError::I2cTransfer, status, true);
                break;
            }
            const auto decoded = ms5611::decode_adc(response.data(),
                                                     response.size());
            if (!decoded.ok()) {
                fail(completed_us, DriverError::InvalidAdc,
                     interfaces::I2cStatus::Ok, false);
                break;
            }
            raw_temperature_d2_ = decoded.value;
            temperature_valid_ = true;
            last_temperature_timestamp_us_ = completed_us;
            health_.last_i2c_status = status;
            state_ = Ms5611State::Idle;
            deadline_us_ = completed_us;
            break;
        }

        case Ms5611State::StartPressureD1: {
            result.bus_operation = true;
            const auto profile = ms5611::conversion_profile(
                ms5611::ConversionKind::PressureD1, config_.pressure_osr);
            const auto status = write_command(profile.command);
            const auto completed_us = std::max(now_us, clock_.now_us());
            if (status != interfaces::I2cStatus::Ok) {
                fail(completed_us, DriverError::I2cTransfer, status, true);
                break;
            }
            health_.last_i2c_status = status;
            next_pressure_due_us_ = advance_periodic_deadline(
                next_pressure_due_us_, completed_us,
                config_.pressure_interval_us);
            state_ = Ms5611State::WaitForPressureD1;
            deadline_us_ = saturating_add(
                completed_us,
                static_cast<std::uint64_t>(profile.maximum_duration_us) +
                    config_.conversion_margin_us);
            break;
        }

        case Ms5611State::WaitForPressureD1:
            state_ = Ms5611State::ReadPressureD1;
            deadline_us_ = now_us;
            break;

        case Ms5611State::ReadPressureD1: {
            std::array<std::uint8_t, 3> response{};
            result.bus_operation = true;
            const auto status = read_adc(response);
            const auto completed_us = std::max(now_us, clock_.now_us());
            if (status != interfaces::I2cStatus::Ok) {
                fail(completed_us, DriverError::I2cTransfer, status, true);
                break;
            }
            const auto decoded = ms5611::decode_adc(response.data(),
                                                     response.size());
            if (!decoded.ok()) {
                fail(completed_us, DriverError::InvalidAdc,
                     interfaces::I2cStatus::Ok, false);
                break;
            }

            const auto temperature_age =
                completed_us >= last_temperature_timestamp_us_
                    ? completed_us - last_temperature_timestamp_us_
                    : static_cast<std::uint64_t>(
                          config_.maximum_temperature_age_us) +
                          1U;
            if (!temperature_valid_ ||
                temperature_age > config_.maximum_temperature_age_us) {
                fail(completed_us, DriverError::StaleTemperature,
                     interfaces::I2cStatus::Ok, false);
                break;
            }

            const auto compensated = ms5611::compensate(
                calibration_, decoded.value, raw_temperature_d2_);
            if (!prom_valid_ || !compensated.ok()) {
                fail(completed_us, DriverError::Compensation,
                     interfaces::I2cStatus::Ok, false);
                break;
            }
            last_pressure_timestamp_us_ = completed_us;
            publish_sample(compensated.value, completed_us, result);
            health_.last_i2c_status = status;
            state_ = Ms5611State::Idle;
            deadline_us_ = next_deadline_us();
            break;
        }

        case Ms5611State::RetryBackoff:
            if (recovery_required_) {
                state_ = Ms5611State::RecoverBus;
                deadline_us_ = now_us;
            } else {
                restart_after_backoff(now_us);
            }
            break;

        case Ms5611State::RecoverBus: {
            result.bus_operation = true;
            const auto status =
                bus_.recover(config_.retry.recovery_timeout_us);
            const auto completed_us = std::max(now_us, clock_.now_us());
            result.bus_settle_until_us =
                saturating_add(completed_us, kRecoverySettleUs);
            if (status != interfaces::I2cStatus::Ok) {
                saturating_increment(health_.recovery_failures);
                fail(completed_us, DriverError::I2cRecovery, status, false);
                break;
            }
            saturating_increment(health_.recoveries);
            health_.last_i2c_status = status;
            recovery_required_ = false;
            state_ = Ms5611State::RecoverySettle;
            deadline_us_ = result.bus_settle_until_us;
            break;
        }

        case Ms5611State::RecoverySettle:
            restart_after_backoff(now_us);
            break;

        case Ms5611State::Offline:
            health_.consecutive_failures = 0U;
            retry_backoff_us_ = config_.retry.initial_backoff_us;
            restart_after_backoff(now_us);
            break;
    }

    return result;
}

SharedSensorScheduler::SharedSensorScheduler(
    interfaces::II2cBus& bus, interfaces::IMonotonicClock& clock,
    interfaces::ISamplePublisher& publisher, Sht3xConfig sht_config,
    Ms5611Config ms_config) noexcept
    : clock_(clock),
      sht3x_(bus, clock, publisher, sht_config),
      ms5611_(bus, clock, publisher, ms_config) {}

auto SharedSensorScheduler::step() noexcept -> SchedulerStepResult {
    const auto now_us = clock_.now_us();
    if (now_us < bus_settle_until_us_) {
        return {};
    }
    if (bus_settle_until_us_ != 0U) {
        bus_settle_until_us_ = 0U;
    }
    const bool sht_ready = sht3x_.ready_at(now_us);
    const bool ms_ready = ms5611_.ready_at(now_us);

    if (!sht_ready && !ms_ready) {
        return {};
    }
    SchedulerStepResult scheduled{};
    if (sht_ready && ms_ready) {
        if (next_when_both_ == ScheduledSensor::Sht3x) {
            next_when_both_ = ScheduledSensor::Ms5611;
            scheduled = {ScheduledSensor::Sht3x, sht3x_.step_at(now_us)};
        } else {
            next_when_both_ = ScheduledSensor::Sht3x;
            scheduled = {ScheduledSensor::Ms5611, ms5611_.step_at(now_us)};
        }
    } else if (sht_ready) {
        next_when_both_ = ScheduledSensor::Ms5611;
        scheduled = {ScheduledSensor::Sht3x, sht3x_.step_at(now_us)};
    } else {
        next_when_both_ = ScheduledSensor::Sht3x;
        scheduled = {ScheduledSensor::Ms5611, ms5611_.step_at(now_us)};
    }
    if (scheduled.step.bus_settle_until_us != 0U) {
        bus_settle_until_us_ = scheduled.step.bus_settle_until_us;
        if (scheduled.sensor == ScheduledSensor::Sht3x) {
            ms5611_.invalidate_after_shared_recovery(bus_settle_until_us_);
        } else if (scheduled.sensor == ScheduledSensor::Ms5611) {
            sht3x_.invalidate_after_shared_recovery(bus_settle_until_us_);
        }
    }
    return scheduled;
}

auto SharedSensorScheduler::next_deadline_us() const noexcept
    -> std::uint64_t {
    if (clock_.now_us() < bus_settle_until_us_) {
        return bus_settle_until_us_;
    }
    return std::min(sht3x_.next_deadline_us(),
                    ms5611_.next_deadline_us());
}

auto SharedSensorScheduler::sht3x() const noexcept
    -> const Sht3xStateMachine& {
    return sht3x_;
}

auto SharedSensorScheduler::ms5611() const noexcept
    -> const Ms5611StateMachine& {
    return ms5611_;
}

auto SharedSensorScheduler::set_sht3x_interval_us(const std::uint32_t interval_us) noexcept
    -> bool {
    return sht3x_.set_sample_interval_us(interval_us);
}

auto SharedSensorScheduler::set_ms5611_interval_us(const std::uint32_t interval_us) noexcept
    -> bool {
    return ms5611_.set_pressure_interval_us(interval_us);
}

}  // namespace shahbaz::sensors::scheduler
