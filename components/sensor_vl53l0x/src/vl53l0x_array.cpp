/**
 * @file vl53l0x_array.cpp
 * @brief Cooperative four-device VL53L0X implementation.
 *
 * The bounded state-machine structure is Shahbaz-specific. The static tuning
 * register sequence is adapted from PX4-Autopilot's BSD-3-Clause VL53L0X
 * driver; see ../LICENSE-PX4-BSD-3-Clause.txt.
 */
#include "sensor_vl53l0x/vl53l0x_array.hpp"

#include "shahbaz/domain/measurement.hpp"

#include <algorithm>
#include <array>
#include <iterator>
#include <limits>

namespace shahbaz::sensors::vl53l0x {
namespace {

constexpr std::uint8_t kRegSysrangeStart = 0x00U;
constexpr std::uint8_t kRegSystemSequenceConfig = 0x01U;
constexpr std::uint8_t kRegSystemInterruptConfigGpio = 0x0AU;
constexpr std::uint8_t kRegSystemInterruptClear = 0x0BU;
constexpr std::uint8_t kRegResultInterruptStatus = 0x13U;
constexpr std::uint8_t kRegResultRangeStatus = 0x14U;
constexpr std::uint8_t kRegFinalRangeMinCountRate = 0x44U;
constexpr std::uint8_t kRegDynamicSpadRequested = 0x4EU;
constexpr std::uint8_t kRegDynamicSpadStartOffset = 0x4FU;
constexpr std::uint8_t kRegMsrcConfigControl = 0x60U;
constexpr std::uint8_t kRegGpioHvMuxActiveHigh = 0x84U;
constexpr std::uint8_t kRegVhvPadSclSdaExtsup = 0x89U;
constexpr std::uint8_t kRegI2cAddress = 0x8AU;
constexpr std::uint8_t kRegStopVariable = 0x91U;
constexpr std::uint8_t kRegSpadInfo = 0x92U;
constexpr std::uint8_t kRegModelId = 0xC0U;
constexpr std::uint8_t kRegReferenceSpadMap = 0xB0U;
constexpr std::uint8_t kRegReferenceSpadStart = 0xB6U;
constexpr std::uint32_t kRecoverySettleUs = 1'000U;
constexpr std::uint32_t kMinimumDelayUs = 1U;

constexpr std::uint32_t kHealthInvalidStatus = 1U << 0U;
constexpr std::uint32_t kHealthDistanceOutsideControlRange = 1U << 1U;

struct RegisterWrite final {
    std::uint8_t reg;
    std::uint8_t value;
};

constexpr RegisterWrite kPrelude[] = {
    {0x88U, 0x00U}, {0x80U, 0x01U}, {0xFFU, 0x01U}, {0x00U, 0x00U},
};
constexpr RegisterWrite kAfterStopVariable[] = {
    {0x00U, 0x01U}, {0xFFU, 0x00U}, {0x80U, 0x00U},
};
constexpr RegisterWrite kSpadRequestPrelude[] = {
    {0x80U, 0x01U}, {0xFFU, 0x01U}, {0x00U, 0x00U}, {0xFFU, 0x06U},
};
constexpr RegisterWrite kSpadRequestStart[] = {
    {0xFFU, 0x07U}, {0x81U, 0x01U}, {0x80U, 0x01U},
    {0x94U, 0x6BU}, {0x83U, 0x00U},
};
constexpr RegisterWrite kSpadCleanupPrefix[] = {
    {0x81U, 0x00U}, {0xFFU, 0x06U},
};
constexpr RegisterWrite kSpadCleanupSuffix[] = {
    {0xFFU, 0x01U}, {0x00U, 0x01U}, {0xFFU, 0x00U}, {0x80U, 0x00U},
};
constexpr RegisterWrite kSpadMapSetup[] = {
    {0xFFU, 0x01U},
    {kRegDynamicSpadStartOffset, 0x00U},
    {kRegDynamicSpadRequested, 0x2CU},
    {0xFFU, 0x00U},
    {kRegReferenceSpadStart, 0xB4U},
};

// The values below are the ST "magic" tuning settings carried by PX4. They
// are intentionally data, not a blocking initializer: one write is issued per
// scheduler step and every transport error is checked.
constexpr RegisterWrite kTuning[] = {
    {0xFFU, 0x01U}, {0x00U, 0x00U}, {0xFFU, 0x00U}, {0x09U, 0x00U},
    {0x10U, 0x00U}, {0x11U, 0x00U}, {0x24U, 0x01U}, {0x25U, 0xFFU},
    {0x75U, 0x00U}, {0xFFU, 0x01U}, {0x4EU, 0x2CU}, {0x48U, 0x00U},
    {0x30U, 0x20U}, {0xFFU, 0x00U}, {0x30U, 0x09U}, {0x54U, 0x00U},
    {0x31U, 0x04U}, {0x32U, 0x03U}, {0x40U, 0x83U}, {0x46U, 0x25U},
    {0x60U, 0x00U}, {0x27U, 0x00U}, {0x50U, 0x06U}, {0x51U, 0x00U},
    {0x52U, 0x96U}, {0x56U, 0x08U}, {0x57U, 0x30U}, {0x61U, 0x00U},
    {0x62U, 0x00U}, {0x64U, 0x00U}, {0x65U, 0x00U}, {0x66U, 0xA0U},
    {0xFFU, 0x01U}, {0x22U, 0x32U}, {0x47U, 0x14U}, {0x49U, 0xFFU},
    {0x4AU, 0x00U}, {0xFFU, 0x00U}, {0x7AU, 0x0AU}, {0x7BU, 0x00U},
    {0x78U, 0x21U}, {0xFFU, 0x01U}, {0x23U, 0x34U}, {0x42U, 0x00U},
    {0x44U, 0xFFU}, {0x45U, 0x26U}, {0x46U, 0x05U}, {0x40U, 0x40U},
    {0x0EU, 0x06U}, {0x20U, 0x1AU}, {0x43U, 0x40U}, {0xFFU, 0x00U},
    {0x34U, 0x03U}, {0x35U, 0x44U}, {0xFFU, 0x01U}, {0x31U, 0x04U},
    {0x4BU, 0x09U}, {0x4CU, 0x05U}, {0x4DU, 0x04U}, {0xFFU, 0x00U},
    {0x44U, 0x00U}, {0x45U, 0x20U}, {0x47U, 0x08U}, {0x48U, 0x28U},
    {0x67U, 0x00U}, {0x70U, 0x04U}, {0x71U, 0x01U}, {0x72U, 0xFEU},
    {0x76U, 0x00U}, {0x77U, 0x00U}, {0xFFU, 0x01U}, {0x0DU, 0x01U},
    {0xFFU, 0x00U}, {0x80U, 0x01U}, {0x01U, 0xF8U}, {0xFFU, 0x01U},
    {0x8EU, 0x01U}, {0x00U, 0x01U}, {0xFFU, 0x00U}, {0x80U, 0x00U},
};

constexpr RegisterWrite kMeasurementPrelude[] = {
    {0x80U, 0x01U}, {0xFFU, 0x01U}, {0x00U, 0x00U},
    // Register 0x91 is written separately because its value is sensor-specific.
};
constexpr RegisterWrite kMeasurementAfterStop[] = {
    {0x00U, 0x01U}, {0xFFU, 0x00U}, {0x80U, 0x00U},
};

[[nodiscard]] constexpr auto saturating_add(const std::uint64_t value,
                                             const std::uint64_t increment) noexcept
    -> std::uint64_t {
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    return increment > maximum - value ? maximum : value + increment;
}

void saturating_increment(std::uint32_t& value) noexcept {
    if (value != std::numeric_limits<std::uint32_t>::max()) ++value;
}

[[nodiscard]] constexpr auto requires_bus_recovery(
    const interfaces::I2cStatus status) noexcept -> bool {
    return status == interfaces::I2cStatus::Timeout ||
           status == interfaces::I2cStatus::BusError ||
           status == interfaces::I2cStatus::ArbitrationLost;
}

[[nodiscard]] constexpr auto next_backoff(const std::uint32_t failures,
                                          const ArrayConfig& config) noexcept
    -> std::uint32_t {
    std::uint64_t value = std::max(config.retry_initial_backoff_us, kMinimumDelayUs);
    const auto shifts = std::min<std::uint32_t>(
        failures > 0U ? failures - 1U : std::uint32_t{0U},
        std::uint32_t{31U});
    for (std::uint32_t index = 0U; index < shifts; ++index) {
        value = std::min<std::uint64_t>(value * 2U, config.retry_maximum_backoff_us);
    }
    return static_cast<std::uint32_t>(
        std::min<std::uint64_t>(value, config.retry_maximum_backoff_us));
}

}  // namespace

enum class ArrayDriver::ConfigurePhase : std::uint8_t {
    ReadVhv,
    WriteVhv,
    Prelude,
    ReadStopVariable,
    AfterStopVariable,
    ReadMsrc,
    WriteMsrc,
    WriteRateLimit,
    WriteFullSequence,
    SpadRequestPrelude,
    ReadSpadControl,
    WriteSpadControlSet,
    SpadRequestStart,
    PollSpadReady,
    AcknowledgeSpadReady,
    ReadSpadInfo,
    SpadCleanupPrefix,
    ReadSpadControlCleanup,
    WriteSpadControlCleanup,
    SpadCleanupSuffix,
    ReadSpadMap,
    SpadMapSetup,
    WriteSpadMap,
    Tuning,
    ConfigureInterrupt,
    ReadInterruptPolarity,
    WriteInterruptPolarity,
    ClearInterrupt,
    RestoreSequenceBeforeCalibration,
    SelectVhvCalibration,
    StartVhvCalibration,
    PollVhvCalibration,
    ClearVhvCalibration,
    StopVhvCalibration,
    SelectPhaseCalibration,
    StartPhaseCalibration,
    PollPhaseCalibration,
    ClearPhaseCalibration,
    StopPhaseCalibration,
    RestoreFinalSequence,
    Complete,
};

enum class ArrayDriver::MeasurePhase : std::uint8_t {
    Prelude,
    StopVariable,
    AfterStopVariable,
    Start,
};

ArrayDriver::ArrayDriver(
    interfaces::II2cBus& bus, interfaces::IMonotonicClock& clock,
    interfaces::ISamplePublisher& publisher,
    interfaces::ISensorShutdownBank& shutdown, ArrayConfig config) noexcept
    : bus_(bus),
      clock_(clock),
      publisher_(publisher),
      shutdown_(shutdown),
      config_(config),
      configure_phase_(ConfigurePhase::ReadVhv),
      measure_phase_(MeasurePhase::Prelude) {
    if (!config_.enabled) {
        for (auto& item : health_) item.last_error = DriverError::Disabled;
        state_ = ArrayState::Disabled;
        return;
    }
    if (!valid_config()) {
        for (auto& item : health_) {
            item.configuration_valid = false;
            item.last_error = DriverError::InvalidConfiguration;
        }
        state_ = ArrayState::Fault;
        return;
    }
    state_ = ArrayState::HoldAllShutdown;
    deadline_us_ = clock_.now_us();
}

auto ArrayDriver::valid_config() const noexcept -> bool {
    if (shutdown_.channel_count() != kSensorCount ||
        config_.transaction_timeout_us == 0U || config_.shutdown_settle_us == 0U ||
        config_.boot_wait_us == 0U || config_.initialization_poll_interval_us == 0U ||
        config_.initialization_timeout_us == 0U ||
        config_.measurement_poll_interval_us == 0U ||
        config_.measurement_timeout_us < kMinimumTimingBudgetUs ||
        config_.per_sensor_sample_interval_us < config_.measurement_timeout_us ||
        config_.retry_initial_backoff_us == 0U ||
        config_.retry_maximum_backoff_us < config_.retry_initial_backoff_us ||
        config_.bus_recovery_timeout_us == 0U) {
        return false;
    }
    for (std::size_t index = 0U; index < kSensorCount; ++index) {
        const auto address = config_.assigned_addresses[index];
        if (!is_usable_seven_bit_address(address) || address == kDefaultAddress ||
            address == 0x44U || address == 0x45U ||
            address == 0x76U || address == 0x77U) {
            return false;
        }
        for (std::size_t other = index + 1U; other < kSensorCount; ++other) {
            if (address == config_.assigned_addresses[other]) return false;
        }
    }
    return true;
}

auto ArrayDriver::write_register(const std::uint8_t address,
                                 const std::uint8_t reg,
                                 const std::uint8_t value) noexcept
    -> interfaces::I2cStatus {
    const std::array<std::uint8_t, 2U> bytes{{reg, value}};
    return bus_.transfer({address,
                          {bytes.data(), bytes.size()},
                          {},
                          config_.transaction_timeout_us,
                          interfaces::I2cWriteReadBoundary::StopThenStart});
}

auto ArrayDriver::write_block(const std::uint8_t address,
                              const std::uint8_t reg,
                              const std::uint8_t* const data,
                              const std::size_t size) noexcept
    -> interfaces::I2cStatus {
    if (data == nullptr || size == 0U || size > 12U) {
        return interfaces::I2cStatus::InvalidArgument;
    }
    std::array<std::uint8_t, 13U> bytes{};
    bytes[0] = reg;
    std::copy_n(data, size, bytes.data() + 1U);
    return bus_.transfer({address,
                          {bytes.data(), size + 1U},
                          {},
                          config_.transaction_timeout_us,
                          interfaces::I2cWriteReadBoundary::StopThenStart});
}

auto ArrayDriver::read_block(const std::uint8_t address,
                             const std::uint8_t reg,
                             std::uint8_t* const data,
                             const std::size_t size) noexcept
    -> interfaces::I2cStatus {
    if (data == nullptr || size == 0U) return interfaces::I2cStatus::InvalidArgument;
    return bus_.transfer({address,
                          {&reg, 1U},
                          {data, size},
                          config_.transaction_timeout_us,
                          interfaces::I2cWriteReadBoundary::StopThenStart});
}

void ArrayDriver::schedule_retry(const std::size_t instance,
                                 const std::uint64_t now_us) noexcept {
    retry_at_us_[instance] = saturating_add(
        now_us, next_backoff(health_[instance].consecutive_failures, config_));
}

void ArrayDriver::isolate_current(const std::uint64_t now_us) noexcept {
    const auto status = shutdown_.set_enabled(current_, false);
    if (status != interfaces::ShutdownStatus::Ok) {
        health_[current_].last_error = DriverError::ShutdownControl;
        health_[current_].configuration_valid = false;
        state_ = ArrayState::Fault;
        return;
    }
    address_assigned_[current_] = false;
    health_[current_].online = false;
    schedule_retry(current_, now_us);
}

void ArrayDriver::fail_current(const std::uint64_t now_us,
                               const DriverError error,
                               const interfaces::I2cStatus i2c_status,
                               const bool shared_bus_fault) noexcept {
    auto& item = health_[current_];
    item.last_error = error;
    item.last_i2c_status = i2c_status;
    item.online = false;
    recovered_since_sample_[current_] = true;
    saturating_increment(item.consecutive_failures);
    saturating_increment(item.total_failures);
    if (i2c_status != interfaces::I2cStatus::Ok) saturating_increment(item.i2c_failures);
    if (error == DriverError::UnexpectedIdentity) saturating_increment(item.identity_failures);
    if (error == DriverError::InitializationTimeout) {
        saturating_increment(item.initialization_timeouts);
    }
    if (error == DriverError::MeasurementTimeout) {
        saturating_increment(item.measurement_timeouts);
    }
    pending_failure_error_ = error;
    pending_failure_i2c_ = i2c_status;
    isolate_current(now_us);
    if (state_ == ArrayState::Fault) return;
    state_ = shared_bus_fault ? ArrayState::RecoverBus : ArrayState::SelectWork;
    deadline_us_ = now_us;
}

auto ArrayDriver::configure_current(const std::uint64_t now_us,
                                    StepResult& result) noexcept -> bool {
    const auto address = config_.assigned_addresses[current_];
    const auto execute_write = [&](const std::uint8_t reg,
                                   const std::uint8_t value) noexcept {
        result.bus_operation = true;
        const auto status = write_register(address, reg, value);
        if (status != interfaces::I2cStatus::Ok) {
            fail_current(now_us, DriverError::I2cTransfer, status,
                         requires_bus_recovery(status));
            return false;
        }
        result.state_advanced = true;
        return true;
    };
    const auto execute_read = [&](const std::uint8_t reg, std::uint8_t* data,
                                  const std::size_t size) noexcept {
        result.bus_operation = true;
        const auto status = read_block(address, reg, data, size);
        if (status != interfaces::I2cStatus::Ok) {
            fail_current(now_us, DriverError::I2cTransfer, status,
                         requires_bus_recovery(status));
            return false;
        }
        result.state_advanced = true;
        return true;
    };
    const auto execute_script = [&](const RegisterWrite* const script,
                                    const std::size_t count,
                                    const ConfigurePhase next) noexcept {
        const auto operation = script[script_index_];
        if (!execute_write(operation.reg, operation.value)) return false;
        ++script_index_;
        if (script_index_ >= count) {
            script_index_ = 0U;
            configure_phase_ = next;
        }
        return true;
    };

    switch (configure_phase_) {
        case ConfigurePhase::ReadVhv:
            if (execute_read(kRegVhvPadSclSdaExtsup, &scratch_, 1U)) {
                configure_phase_ = ConfigurePhase::WriteVhv;
            }
            break;
        case ConfigurePhase::WriteVhv:
            if (execute_write(kRegVhvPadSclSdaExtsup,
                              static_cast<std::uint8_t>(scratch_ | 0x01U))) {
                configure_phase_ = ConfigurePhase::Prelude;
            }
            break;
        case ConfigurePhase::Prelude:
            (void)execute_script(kPrelude, std::size(kPrelude),
                                 ConfigurePhase::ReadStopVariable);
            break;
        case ConfigurePhase::ReadStopVariable:
            if (execute_read(kRegStopVariable, &stop_variables_[current_], 1U)) {
                configure_phase_ = ConfigurePhase::AfterStopVariable;
            }
            break;
        case ConfigurePhase::AfterStopVariable:
            (void)execute_script(kAfterStopVariable, std::size(kAfterStopVariable),
                                 ConfigurePhase::ReadMsrc);
            break;
        case ConfigurePhase::ReadMsrc:
            if (execute_read(kRegMsrcConfigControl, &scratch_, 1U)) {
                configure_phase_ = ConfigurePhase::WriteMsrc;
            }
            break;
        case ConfigurePhase::WriteMsrc:
            if (execute_write(kRegMsrcConfigControl,
                              static_cast<std::uint8_t>(scratch_ | 0x12U))) {
                configure_phase_ = ConfigurePhase::WriteRateLimit;
            }
            break;
        case ConfigurePhase::WriteRateLimit: {
            // FINAL_RANGE_CONFIG_MIN_COUNT_RATE_RTN_LIMIT is Q9.7. The ST
            // default 0.25 MCPS is therefore 0x0020 (MSB first).
            constexpr std::array<std::uint8_t, 2U> kRateLimit{{0x00U, 0x20U}};
            result.bus_operation = true;
            const auto status = write_block(address, kRegFinalRangeMinCountRate,
                                            kRateLimit.data(), kRateLimit.size());
            if (status != interfaces::I2cStatus::Ok) {
                fail_current(now_us, DriverError::I2cTransfer, status,
                             requires_bus_recovery(status));
            } else {
                configure_phase_ = ConfigurePhase::WriteFullSequence;
                result.state_advanced = true;
            }
            break;
        }
        case ConfigurePhase::WriteFullSequence:
            if (execute_write(kRegSystemSequenceConfig, 0xFFU)) {
                configure_phase_ = ConfigurePhase::SpadRequestPrelude;
            }
            break;
        case ConfigurePhase::SpadRequestPrelude:
            (void)execute_script(kSpadRequestPrelude, std::size(kSpadRequestPrelude),
                                 ConfigurePhase::ReadSpadControl);
            break;
        case ConfigurePhase::ReadSpadControl:
            if (execute_read(0x83U, &scratch_, 1U)) {
                configure_phase_ = ConfigurePhase::WriteSpadControlSet;
            }
            break;
        case ConfigurePhase::WriteSpadControlSet:
            if (execute_write(0x83U, static_cast<std::uint8_t>(scratch_ | 0x04U))) {
                configure_phase_ = ConfigurePhase::SpadRequestStart;
            }
            break;
        case ConfigurePhase::SpadRequestStart:
            if (execute_script(kSpadRequestStart, std::size(kSpadRequestStart),
                               ConfigurePhase::PollSpadReady) &&
                configure_phase_ == ConfigurePhase::PollSpadReady) {
                operation_timeout_at_us_ =
                    saturating_add(now_us, config_.initialization_timeout_us);
            }
            break;
        case ConfigurePhase::PollSpadReady:
            if (!execute_read(0x83U, &scratch_, 1U)) break;
            if (scratch_ != 0U) {
                configure_phase_ = ConfigurePhase::AcknowledgeSpadReady;
            } else if (now_us >= operation_timeout_at_us_) {
                fail_current(now_us, DriverError::InitializationTimeout,
                             interfaces::I2cStatus::Ok, false);
            } else {
                deadline_us_ = saturating_add(now_us,
                    config_.initialization_poll_interval_us);
            }
            break;
        case ConfigurePhase::AcknowledgeSpadReady:
            if (execute_write(0x83U, 0x01U)) {
                configure_phase_ = ConfigurePhase::ReadSpadInfo;
            }
            break;
        case ConfigurePhase::ReadSpadInfo:
            if (execute_read(kRegSpadInfo, &scratch_, 1U)) {
                requested_spad_count_ = static_cast<std::uint8_t>(scratch_ & 0x7FU);
                requested_spads_aperture_ = (scratch_ & 0x80U) != 0U;
                configure_phase_ = ConfigurePhase::SpadCleanupPrefix;
            }
            break;
        case ConfigurePhase::SpadCleanupPrefix:
            (void)execute_script(kSpadCleanupPrefix, std::size(kSpadCleanupPrefix),
                                 ConfigurePhase::ReadSpadControlCleanup);
            break;
        case ConfigurePhase::ReadSpadControlCleanup:
            if (execute_read(0x83U, &scratch_, 1U)) {
                configure_phase_ = ConfigurePhase::WriteSpadControlCleanup;
            }
            break;
        case ConfigurePhase::WriteSpadControlCleanup:
            if (execute_write(0x83U,
                              static_cast<std::uint8_t>(scratch_ & ~0x04U))) {
                configure_phase_ = ConfigurePhase::SpadCleanupSuffix;
            }
            break;
        case ConfigurePhase::SpadCleanupSuffix:
            (void)execute_script(kSpadCleanupSuffix, std::size(kSpadCleanupSuffix),
                                 ConfigurePhase::ReadSpadMap);
            break;
        case ConfigurePhase::ReadSpadMap:
            if (execute_read(kRegReferenceSpadMap, reference_spad_map_.data(),
                             reference_spad_map_.size())) {
                if (!apply_reference_spad_selection(reference_spad_map_,
                                                    requested_spad_count_,
                                                    requested_spads_aperture_)) {
                    fail_current(now_us, DriverError::ReferenceSpadConfiguration,
                                 interfaces::I2cStatus::Ok, false);
                } else {
                    configure_phase_ = ConfigurePhase::SpadMapSetup;
                }
            }
            break;
        case ConfigurePhase::SpadMapSetup:
            (void)execute_script(kSpadMapSetup, std::size(kSpadMapSetup),
                                 ConfigurePhase::WriteSpadMap);
            break;
        case ConfigurePhase::WriteSpadMap: {
            result.bus_operation = true;
            const auto status = write_block(address, kRegReferenceSpadMap,
                                            reference_spad_map_.data(),
                                            reference_spad_map_.size());
            if (status != interfaces::I2cStatus::Ok) {
                fail_current(now_us, DriverError::I2cTransfer, status,
                             requires_bus_recovery(status));
            } else {
                configure_phase_ = ConfigurePhase::Tuning;
                result.state_advanced = true;
            }
            break;
        }
        case ConfigurePhase::Tuning:
            (void)execute_script(kTuning, std::size(kTuning),
                                 ConfigurePhase::ConfigureInterrupt);
            break;
        case ConfigurePhase::ConfigureInterrupt:
            if (execute_write(kRegSystemInterruptConfigGpio, 0x04U)) {
                configure_phase_ = ConfigurePhase::ReadInterruptPolarity;
            }
            break;
        case ConfigurePhase::ReadInterruptPolarity:
            if (execute_read(kRegGpioHvMuxActiveHigh, &scratch_, 1U)) {
                configure_phase_ = ConfigurePhase::WriteInterruptPolarity;
            }
            break;
        case ConfigurePhase::WriteInterruptPolarity:
            if (execute_write(kRegGpioHvMuxActiveHigh,
                              static_cast<std::uint8_t>(scratch_ & ~0x10U))) {
                configure_phase_ = ConfigurePhase::ClearInterrupt;
            }
            break;
        case ConfigurePhase::ClearInterrupt:
            if (execute_write(kRegSystemInterruptClear, 0x01U)) {
                configure_phase_ = ConfigurePhase::RestoreSequenceBeforeCalibration;
            }
            break;
        case ConfigurePhase::RestoreSequenceBeforeCalibration:
            if (execute_write(kRegSystemSequenceConfig, 0xE8U)) {
                configure_phase_ = ConfigurePhase::SelectVhvCalibration;
            }
            break;
        case ConfigurePhase::SelectVhvCalibration:
            if (execute_write(kRegSystemSequenceConfig, 0x01U)) {
                configure_phase_ = ConfigurePhase::StartVhvCalibration;
            }
            break;
        case ConfigurePhase::StartVhvCalibration:
            if (execute_write(kRegSysrangeStart, 0x41U)) {
                operation_timeout_at_us_ =
                    saturating_add(now_us, config_.initialization_timeout_us);
                configure_phase_ = ConfigurePhase::PollVhvCalibration;
            }
            break;
        case ConfigurePhase::PollVhvCalibration:
            if (!execute_read(kRegResultInterruptStatus, &scratch_, 1U)) break;
            if ((scratch_ & 0x07U) != 0U) {
                configure_phase_ = ConfigurePhase::ClearVhvCalibration;
            } else if (now_us >= operation_timeout_at_us_) {
                fail_current(now_us, DriverError::InitializationTimeout,
                             interfaces::I2cStatus::Ok, false);
            } else {
                deadline_us_ = saturating_add(now_us,
                    config_.initialization_poll_interval_us);
            }
            break;
        case ConfigurePhase::ClearVhvCalibration:
            if (execute_write(kRegSystemInterruptClear, 0x01U)) {
                configure_phase_ = ConfigurePhase::StopVhvCalibration;
            }
            break;
        case ConfigurePhase::StopVhvCalibration:
            if (execute_write(kRegSysrangeStart, 0x00U)) {
                configure_phase_ = ConfigurePhase::SelectPhaseCalibration;
            }
            break;
        case ConfigurePhase::SelectPhaseCalibration:
            if (execute_write(kRegSystemSequenceConfig, 0x02U)) {
                configure_phase_ = ConfigurePhase::StartPhaseCalibration;
            }
            break;
        case ConfigurePhase::StartPhaseCalibration:
            if (execute_write(kRegSysrangeStart, 0x01U)) {
                operation_timeout_at_us_ =
                    saturating_add(now_us, config_.initialization_timeout_us);
                configure_phase_ = ConfigurePhase::PollPhaseCalibration;
            }
            break;
        case ConfigurePhase::PollPhaseCalibration:
            if (!execute_read(kRegResultInterruptStatus, &scratch_, 1U)) break;
            if ((scratch_ & 0x07U) != 0U) {
                configure_phase_ = ConfigurePhase::ClearPhaseCalibration;
            } else if (now_us >= operation_timeout_at_us_) {
                fail_current(now_us, DriverError::InitializationTimeout,
                             interfaces::I2cStatus::Ok, false);
            } else {
                deadline_us_ = saturating_add(now_us,
                    config_.initialization_poll_interval_us);
            }
            break;
        case ConfigurePhase::ClearPhaseCalibration:
            if (execute_write(kRegSystemInterruptClear, 0x01U)) {
                configure_phase_ = ConfigurePhase::StopPhaseCalibration;
            }
            break;
        case ConfigurePhase::StopPhaseCalibration:
            if (execute_write(kRegSysrangeStart, 0x00U)) {
                configure_phase_ = ConfigurePhase::RestoreFinalSequence;
            }
            break;
        case ConfigurePhase::RestoreFinalSequence:
            if (execute_write(kRegSystemSequenceConfig, 0xE8U)) {
                configure_phase_ = ConfigurePhase::Complete;
            }
            break;
        case ConfigurePhase::Complete:
            configuration_complete(now_us);
            result.state_advanced = true;
            break;
    }
    return true;
}

void ArrayDriver::configuration_complete(const std::uint64_t now_us) noexcept {
    auto& item = health_[current_];
    item.online = true;
    item.last_error = DriverError::None;
    item.last_i2c_status = interfaces::I2cStatus::Ok;
    item.consecutive_failures = 0U;
    next_sample_at_us_[current_] = now_us;
    configure_phase_ = ConfigurePhase::ReadVhv;
    script_index_ = 0U;
    state_ = ArrayState::SelectWork;
    deadline_us_ = now_us;
}

auto ArrayDriver::prepare_measurement(const std::uint64_t now_us,
                                      StepResult& result) noexcept -> bool {
    const auto address = config_.assigned_addresses[current_];
    const auto execute = [&](const std::uint8_t reg,
                             const std::uint8_t value) noexcept {
        result.bus_operation = true;
        const auto status = write_register(address, reg, value);
        if (status != interfaces::I2cStatus::Ok) {
            fail_current(now_us, DriverError::I2cTransfer, status,
                         requires_bus_recovery(status));
            return false;
        }
        result.state_advanced = true;
        return true;
    };
    switch (measure_phase_) {
        case MeasurePhase::Prelude: {
            const auto operation = kMeasurementPrelude[script_index_];
            if (!execute(operation.reg, operation.value)) return false;
            ++script_index_;
            if (script_index_ == std::size(kMeasurementPrelude)) {
                script_index_ = 0U;
                measure_phase_ = MeasurePhase::StopVariable;
            }
            break;
        }
        case MeasurePhase::StopVariable:
            if (execute(kRegStopVariable, stop_variables_[current_])) {
                measure_phase_ = MeasurePhase::AfterStopVariable;
            }
            break;
        case MeasurePhase::AfterStopVariable: {
            const auto operation = kMeasurementAfterStop[script_index_];
            if (!execute(operation.reg, operation.value)) return false;
            ++script_index_;
            if (script_index_ == std::size(kMeasurementAfterStop)) {
                script_index_ = 0U;
                measure_phase_ = MeasurePhase::Start;
            }
            break;
        }
        case MeasurePhase::Start:
            // Bracket the physical acquisition from before the start-command
            // transfer through completion of the later result read. The
            // interrupt polls only narrow readiness; they are not observations.
            measurement_started_us_ = std::max(now_us, clock_.now_us());
            if (execute(kRegSysrangeStart, 0x01U)) {
                const auto completed_us = std::max(now_us, clock_.now_us());
                measure_phase_ = MeasurePhase::Prelude;
                operation_timeout_at_us_ =
                    saturating_add(completed_us, config_.measurement_timeout_us);
                state_ = ArrayState::WaitForMeasurementStart;
                deadline_us_ = saturating_add(completed_us,
                    config_.measurement_poll_interval_us);
            }
            break;
    }
    return true;
}

void ArrayDriver::select_work(const std::uint64_t now_us) noexcept {
    std::uint64_t earliest = std::numeric_limits<std::uint64_t>::max();
    for (std::size_t offset = 0U; offset < kSensorCount; ++offset) {
        const auto instance = (round_robin_cursor_ + offset) % kSensorCount;
        if (!health_[instance].online) {
            if (retry_at_us_[instance] <= now_us) {
                current_ = instance;
                round_robin_cursor_ = (instance + 1U) % kSensorCount;
                state_ = ArrayState::EnableDevice;
                deadline_us_ = now_us;
                return;
            }
            earliest = std::min(earliest, retry_at_us_[instance]);
        }
    }
    for (std::size_t offset = 0U; offset < kSensorCount; ++offset) {
        const auto instance = (round_robin_cursor_ + offset) % kSensorCount;
        if (health_[instance].online) {
            if (next_sample_at_us_[instance] <= now_us) {
                current_ = instance;
                round_robin_cursor_ = (instance + 1U) % kSensorCount;
                state_ = ArrayState::PrepareMeasurement;
                measure_phase_ = MeasurePhase::Prelude;
                script_index_ = 0U;
                deadline_us_ = now_us;
                return;
            }
            earliest = std::min(earliest, next_sample_at_us_[instance]);
        }
    }
    state_ = ArrayState::Idle;
    deadline_us_ = earliest;
}

void ArrayDriver::publish_current(const std::uint64_t now_us,
                                  StepResult& result) noexcept {
    auto validity = domain::ValidityFlag::TransportValid |
                    domain::ValidityFlag::CalibrationValid |
                    domain::ValidityFlag::TimingValid;
    std::uint32_t health_flags = 0U;
    if (pending_reading_.control_eligible()) {
        validity = validity | domain::ValidityFlag::PlausibilityValid;
    } else {
        saturating_increment(health_[current_].invalid_measurements);
        if (!pending_reading_.status_valid) health_flags |= kHealthInvalidStatus;
        if (!pending_reading_.distance_within_control_range) {
            health_flags |= kHealthDistanceOutsideControlRange;
        }
    }
    auto quality = domain::QualityFlag::Fresh;
    if (recovered_since_sample_[current_]) {
        quality = quality | domain::QualityFlag::RecoveredAfterError;
    }
    domain::SensorSample sample{};
    sample.sensor_id = domain::SensorId::Vl53l0x;
    sample.instance_id = static_cast<std::uint8_t>(current_);
    sample.sequence = sequence_[current_]++;
    sample.monotonic_timestamp_us = pending_measurement_timestamp_us_;
    sample.validity = validity;
    sample.quality = quality;
    sample.health_flags = health_flags;
    sample.field_count = 4U;
    sample.fields[0] = domain::make_unsigned_field(
        domain::FieldId::DistanceMillimeters, pending_reading_.distance_mm);
    sample.fields[1] = domain::make_unsigned_field(
        domain::FieldId::RangeStatus, pending_reading_.raw_status);
    sample.fields[2] = domain::make_unsigned_field(
        domain::FieldId::SignalQualityPercent,
        pending_reading_.signal_quality_percent);
    sample.fields[3] = domain::make_unsigned_field(
        domain::FieldId::AcquisitionTimeUncertaintyMicros,
        pending_acquisition_uncertainty_us_);
    if (publisher_.publish(sample)) {
        result.sample_published = true;
        recovered_since_sample_[current_] = false;
        health_[current_].last_error = DriverError::None;
        health_[current_].last_i2c_status = interfaces::I2cStatus::Ok;
    } else {
        saturating_increment(health_[current_].publication_drops);
        health_[current_].last_error = DriverError::PublisherBackpressure;
    }
    // Requested cadence is start-to-start. Publication/USB queue latency must
    // not silently stretch it or change the observation timestamp.
    next_sample_at_us_[current_] = saturating_add(
        measurement_started_us_, config_.per_sensor_sample_interval_us);
    state_ = ArrayState::SelectWork;
    deadline_us_ = now_us;
    result.state_advanced = true;
}

auto ArrayDriver::step() noexcept -> StepResult {
    return step_at(clock_.now_us());
}

auto ArrayDriver::step_at(const std::uint64_t now_us) noexcept -> StepResult {
    StepResult result{};
    result.instance_id = static_cast<std::uint8_t>(current_);
    if (!ready_at(now_us)) return result;

    switch (state_) {
        case ArrayState::Disabled:
        case ArrayState::Fault:
            break;
        case ArrayState::HoldAllShutdown: {
            result.shutdown_operation = true;
            const auto status = shutdown_.disable_all();
            if (status != interfaces::ShutdownStatus::Ok) {
                for (auto& item : health_) {
                    item.configuration_valid = false;
                    item.last_error = DriverError::ShutdownControl;
                }
                state_ = ArrayState::Fault;
            } else {
                // XSHUT resets the volatile address. Keep software ownership
                // in lockstep with the electrical state, including when this
                // hold is entered after a shared-bus recovery.
                for (std::size_t index = 0U; index < kSensorCount; ++index) {
                    address_assigned_[index] = false;
                    health_[index].online = false;
                }
                state_ = ArrayState::WaitAfterShutdown;
                deadline_us_ = saturating_add(now_us, config_.shutdown_settle_us);
            }
            result.state_advanced = true;
            break;
        }
        case ArrayState::WaitAfterShutdown:
            state_ = ArrayState::SelectWork;
            deadline_us_ = now_us;
            result.state_advanced = true;
            break;
        case ArrayState::SelectWork:
            select_work(now_us);
            result.state_advanced = true;
            break;
        case ArrayState::EnableDevice: {
            result.shutdown_operation = true;
            const auto status = shutdown_.set_enabled(current_, true);
            if (status != interfaces::ShutdownStatus::Ok) {
                fail_current(now_us, DriverError::ShutdownControl,
                             interfaces::I2cStatus::Ok, false);
            } else {
                state_ = ArrayState::WaitForBoot;
                deadline_us_ = saturating_add(now_us, config_.boot_wait_us);
            }
            result.state_advanced = true;
            break;
        }
        case ArrayState::WaitForBoot:
            state_ = ArrayState::ReadDefaultIdentity;
            deadline_us_ = now_us;
            result.state_advanced = true;
            break;
        case ArrayState::ReadDefaultIdentity: {
            std::array<std::uint8_t, 2U> model{};
            result.bus_operation = true;
            const auto status = read_block(kDefaultAddress, kRegModelId,
                                           model.data(), model.size());
            if (status != interfaces::I2cStatus::Ok) {
                fail_current(now_us, DriverError::I2cTransfer, status,
                             requires_bus_recovery(status));
            } else if (!parse_model_id(model.data(), model.size()).ok()) {
                fail_current(now_us, DriverError::UnexpectedIdentity,
                             interfaces::I2cStatus::Ok, false);
            } else {
                state_ = ArrayState::AssignAddress;
                deadline_us_ = now_us;
            }
            result.state_advanced = true;
            break;
        }
        case ArrayState::AssignAddress: {
            result.bus_operation = true;
            const auto status = write_register(kDefaultAddress, kRegI2cAddress,
                                               config_.assigned_addresses[current_]);
            if (status != interfaces::I2cStatus::Ok) {
                fail_current(now_us, DriverError::I2cTransfer, status,
                             requires_bus_recovery(status));
            } else {
                address_assigned_[current_] = true;
                state_ = ArrayState::VerifyAssignedIdentity;
                deadline_us_ = now_us;
            }
            result.state_advanced = true;
            break;
        }
        case ArrayState::VerifyAssignedIdentity: {
            std::array<std::uint8_t, 2U> model{};
            result.bus_operation = true;
            const auto status = read_block(config_.assigned_addresses[current_],
                                           kRegModelId, model.data(), model.size());
            if (status != interfaces::I2cStatus::Ok) {
                fail_current(now_us, DriverError::I2cTransfer, status,
                             requires_bus_recovery(status));
            } else if (!parse_model_id(model.data(), model.size()).ok()) {
                fail_current(now_us, DriverError::UnexpectedIdentity,
                             interfaces::I2cStatus::Ok, false);
            } else {
                configure_phase_ = ConfigurePhase::ReadVhv;
                script_index_ = 0U;
                state_ = ArrayState::ConfigureDevice;
                deadline_us_ = now_us;
            }
            result.state_advanced = true;
            break;
        }
        case ArrayState::ConfigureDevice:
            (void)configure_current(now_us, result);
            break;
        case ArrayState::PrepareMeasurement:
            (void)prepare_measurement(now_us, result);
            break;
        case ArrayState::WaitForMeasurementStart: {
            result.bus_operation = true;
            const auto status = read_block(config_.assigned_addresses[current_],
                                           kRegSysrangeStart, &scratch_, 1U);
            if (status != interfaces::I2cStatus::Ok) {
                fail_current(now_us, DriverError::I2cTransfer, status,
                             requires_bus_recovery(status));
            } else if ((scratch_ & 0x01U) == 0U) {
                state_ = ArrayState::WaitForMeasurement;
                deadline_us_ = saturating_add(now_us,
                    config_.measurement_poll_interval_us);
            } else if (now_us >= operation_timeout_at_us_) {
                fail_current(now_us, DriverError::MeasurementTimeout,
                             interfaces::I2cStatus::Ok, false);
            } else {
                deadline_us_ = saturating_add(now_us,
                    config_.measurement_poll_interval_us);
            }
            result.state_advanced = true;
            break;
        }
        case ArrayState::WaitForMeasurement: {
            result.bus_operation = true;
            const auto status = read_block(config_.assigned_addresses[current_],
                                           kRegResultInterruptStatus, &scratch_, 1U);
            if (status != interfaces::I2cStatus::Ok) {
                fail_current(now_us, DriverError::I2cTransfer, status,
                             requires_bus_recovery(status));
            } else if ((scratch_ & 0x07U) != 0U) {
                state_ = ArrayState::ReadMeasurement;
                deadline_us_ = now_us;
            } else if (now_us >= operation_timeout_at_us_) {
                fail_current(now_us, DriverError::MeasurementTimeout,
                             interfaces::I2cStatus::Ok, false);
            } else {
                deadline_us_ = saturating_add(now_us,
                    config_.measurement_poll_interval_us);
            }
            result.state_advanced = true;
            break;
        }
        case ArrayState::ReadMeasurement: {
            result.bus_operation = true;
            const auto status = read_block(config_.assigned_addresses[current_],
                                           kRegResultRangeStatus,
                                           range_bytes_.data(), range_bytes_.size());
            const auto completed_us = std::max(now_us, clock_.now_us());
            if (status != interfaces::I2cStatus::Ok) {
                fail_current(completed_us, DriverError::I2cTransfer, status,
                             requires_bus_recovery(status));
            } else {
                const auto parsed = parse_range_result(range_bytes_.data(),
                                                       range_bytes_.size());
                if (!parsed.ok()) {
                    fail_current(completed_us, DriverError::InvalidResult,
                                 interfaces::I2cStatus::Ok, false);
                } else {
                    pending_reading_ = parsed.value;
                    const domain::AcquisitionWindow acquisition{
                        measurement_started_us_, completed_us};
                    pending_measurement_timestamp_us_ = acquisition.midpoint_us();
                    pending_acquisition_uncertainty_us_ = acquisition.wire_uncertainty_us();
                    state_ = ArrayState::ClearInterrupt;
                    deadline_us_ = completed_us;
                }
            }
            result.state_advanced = true;
            break;
        }
        case ArrayState::ClearInterrupt: {
            result.bus_operation = true;
            const auto status = write_register(config_.assigned_addresses[current_],
                                               kRegSystemInterruptClear, 0x01U);
            if (status != interfaces::I2cStatus::Ok) {
                fail_current(now_us, DriverError::I2cTransfer, status,
                             requires_bus_recovery(status));
            } else {
                state_ = ArrayState::PublishMeasurement;
                deadline_us_ = now_us;
            }
            result.state_advanced = true;
            break;
        }
        case ArrayState::PublishMeasurement:
            publish_current(now_us, result);
            break;
        case ArrayState::RecoverBus: {
            result.bus_operation = true;
            const auto status = bus_.recover(config_.bus_recovery_timeout_us);
            const auto completed_us = std::max(now_us, clock_.now_us());
            auto& item = health_[current_];
            // Recovery may pulse SCL/SDA or recreate the controller even when
            // it ultimately reports failure. Publish the barrier on both
            // outcomes so the shared scheduler invalidates every I2C client
            // and permits no traffic until the electrical settle interval.
            result.bus_settle_until_us = saturating_add(completed_us, kRecoverySettleUs);
            if (status == interfaces::I2cStatus::Ok) {
                saturating_increment(item.recoveries);
                for (std::size_t index = 0U; index < kSensorCount; ++index) {
                    if (health_[index].online) {
                        recovered_since_sample_[index] = true;
                        health_[index].online = false;
                    }
                }
                state_ = ArrayState::RecoverySettle;
                deadline_us_ = result.bus_settle_until_us;
            } else {
                saturating_increment(item.recovery_failures);
                item.last_error = DriverError::I2cRecovery;
                item.last_i2c_status = status;
                // Even a failed electrical recovery may have disturbed an
                // addressed peer. Reset the whole bank before any new access.
                state_ = ArrayState::HoldAllShutdown;
                deadline_us_ = result.bus_settle_until_us;
            }
            result.state_advanced = true;
            break;
        }
        case ArrayState::RecoverySettle:
            // Treat recovery pulses and controller reinitialization as an
            // array-wide invalidation. Reassert every XSHUT line before
            // assigning address 0x29 to one role at a time again.
            state_ = ArrayState::HoldAllShutdown;
            deadline_us_ = now_us;
            result.state_advanced = true;
            break;
        case ArrayState::Idle:
            state_ = ArrayState::SelectWork;
            deadline_us_ = now_us;
            result.state_advanced = true;
            break;
    }
    return result;
}

auto ArrayDriver::ready_at(const std::uint64_t now_us) const noexcept -> bool {
    return state_ != ArrayState::Disabled && state_ != ArrayState::Fault &&
           now_us >= deadline_us_;
}

auto ArrayDriver::next_deadline_us() const noexcept -> std::uint64_t {
    return state_ == ArrayState::Disabled || state_ == ArrayState::Fault
               ? std::numeric_limits<std::uint64_t>::max()
               : deadline_us_;
}

auto ArrayDriver::state() const noexcept -> ArrayState { return state_; }

auto ArrayDriver::current_instance() const noexcept -> std::uint8_t {
    return static_cast<std::uint8_t>(current_);
}

auto ArrayDriver::health(const std::size_t instance) const noexcept
    -> const DeviceHealth& {
    return health_[instance < kSensorCount ? instance : 0U];
}

auto ArrayDriver::lifecycle(const std::size_t instance) const noexcept -> Lifecycle {
    if (state_ == ArrayState::Disabled) return Lifecycle::DisabledOrAbsent;
    if (instance >= kSensorCount || state_ == ArrayState::Fault) {
        return Lifecycle::Degraded;
    }
    const auto& item = health_[instance];
    if (item.online) return Lifecycle::Live;
    if (item.last_error == DriverError::None) return Lifecycle::Initializing;
    if (item.last_error == DriverError::Disabled) return Lifecycle::DisabledOrAbsent;
    return Lifecycle::Degraded;
}

auto ArrayDriver::configured_address(const std::size_t instance) const noexcept
    -> std::uint8_t {
    return config_.assigned_addresses[instance < kSensorCount ? instance : 0U];
}

auto ArrayDriver::set_sample_interval_us(const std::uint32_t interval_us) noexcept
    -> bool {
    if (interval_us < config_.measurement_timeout_us) return false;
    config_.per_sensor_sample_interval_us = interval_us;
    return true;
}

void ArrayDriver::invalidate_after_shared_recovery(
    const std::uint64_t settle_until_us) noexcept {
    if (state_ == ArrayState::Disabled || state_ == ArrayState::Fault) return;
    for (std::size_t index = 0U; index < kSensorCount; ++index) {
        if (health_[index].online) {
            saturating_increment(health_[index].shared_bus_invalidations);
            recovered_since_sample_[index] = true;
            health_[index].online = false;
        }
    }

    // Never let another client's recovery bypass the initial all-XSHUT hold.
    // That state makes the shared default address unambiguous before any
    // device is released; retain it while applying the settle barrier.
    if (state_ == ArrayState::HoldAllShutdown) {
        deadline_us_ = settle_until_us;
        return;
    }

    const bool device_may_be_active = state_ == ArrayState::WaitForBoot ||
        state_ == ArrayState::ReadDefaultIdentity ||
        state_ == ArrayState::AssignAddress ||
        state_ == ArrayState::VerifyAssignedIdentity ||
        state_ == ArrayState::ConfigureDevice ||
        state_ == ArrayState::PrepareMeasurement ||
        state_ == ArrayState::WaitForMeasurementStart ||
        state_ == ArrayState::WaitForMeasurement ||
        state_ == ArrayState::ReadMeasurement ||
        state_ == ArrayState::ClearInterrupt;
    if (device_may_be_active) {
        auto& item = health_[current_];
        item.online = false;
        item.last_error = DriverError::I2cRecovery;
        recovered_since_sample_[current_] = true;
        saturating_increment(item.consecutive_failures);
        saturating_increment(item.total_failures);
        isolate_current(settle_until_us);
        if (state_ == ArrayState::Fault) return;
    }
    state_ = ArrayState::RecoverySettle;
    deadline_us_ = settle_until_us;
}

}  // namespace shahbaz::sensors::vl53l0x
