/**
 * @file vl53l0x_array_test.cpp
 * @brief Host state/failure tests for the four-device cooperative driver.
 */
#include "sensor_vl53l0x/vl53l0x_array.hpp"

#include "shahbaz/domain/measurement.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

namespace {

using shahbaz::interfaces::ConstByteView;
using shahbaz::interfaces::I2cStatus;
using shahbaz::interfaces::I2cTransaction;
using shahbaz::interfaces::MutableByteView;
namespace vl53 = shahbaz::sensors::vl53l0x;

auto expect(const bool condition, const char* const message) -> int {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        return 1;
    }
    return 0;
}

struct FakeClock final : shahbaz::interfaces::IMonotonicClock {
    std::uint64_t now{};
    [[nodiscard]] auto now_us() const noexcept -> std::uint64_t override { return now; }
};

struct FakeHardware final {
    std::array<bool, vl53::kSensorCount> enabled{};
    std::array<std::uint8_t, vl53::kSensorCount> address{
        {vl53::kDefaultAddress, vl53::kDefaultAddress,
         vl53::kDefaultAddress, vl53::kDefaultAddress}};
    std::array<std::uint16_t, vl53::kSensorCount> distance_mm{
        {500U, 600U, 700U, 800U}};
    std::array<std::uint8_t, vl53::kSensorCount> range_status{};
    std::array<std::uint8_t, vl53::kSensorCount> stop_variable{
        {0x11U, 0x22U, 0x33U, 0x44U}};
    std::array<std::uint8_t, vl53::kSensorCount> restored_stop_variable{};
};

struct FakeShutdown final : shahbaz::interfaces::ISensorShutdownBank {
    explicit FakeShutdown(FakeHardware& hardware) : hardware_(hardware) {}

    FakeHardware& hardware_;
    bool fail_disable_all{};
    std::size_t fail_enable_index{vl53::kSensorCount};
    std::uint32_t operations{};

    [[nodiscard]] auto channel_count() const noexcept -> std::size_t override {
        return vl53::kSensorCount;
    }

    [[nodiscard]] auto set_enabled(const std::size_t index,
                                   const bool enabled) noexcept
        -> shahbaz::interfaces::ShutdownStatus override {
        ++operations;
        if (index >= vl53::kSensorCount ||
            (enabled && index == fail_enable_index)) {
            return index >= vl53::kSensorCount
                       ? shahbaz::interfaces::ShutdownStatus::InvalidArgument
                       : shahbaz::interfaces::ShutdownStatus::HardwareError;
        }
        hardware_.enabled[index] = enabled;
        if (!enabled) hardware_.address[index] = vl53::kDefaultAddress;
        return shahbaz::interfaces::ShutdownStatus::Ok;
    }

    [[nodiscard]] auto disable_all() noexcept
        -> shahbaz::interfaces::ShutdownStatus override {
        ++operations;
        if (fail_disable_all) return shahbaz::interfaces::ShutdownStatus::HardwareError;
        for (std::size_t index = 0U; index < vl53::kSensorCount; ++index) {
            hardware_.enabled[index] = false;
            hardware_.address[index] = vl53::kDefaultAddress;
        }
        return shahbaz::interfaces::ShutdownStatus::Ok;
    }
};

struct WrongSizeShutdown final : shahbaz::interfaces::ISensorShutdownBank {
    [[nodiscard]] auto channel_count() const noexcept -> std::size_t override { return 3U; }
    [[nodiscard]] auto set_enabled(std::size_t, bool) noexcept
        -> shahbaz::interfaces::ShutdownStatus override {
        return shahbaz::interfaces::ShutdownStatus::Ok;
    }
    [[nodiscard]] auto disable_all() noexcept
        -> shahbaz::interfaces::ShutdownStatus override {
        return shahbaz::interfaces::ShutdownStatus::Ok;
    }
};

struct FakeBus final : shahbaz::interfaces::II2cBus {
    explicit FakeBus(FakeHardware& hardware) : hardware_(hardware) {}

    FakeHardware& hardware_;
    std::uint32_t operations{};
    std::uint32_t recoveries{};
    std::uint32_t fail_operation{};
    I2cStatus failure_status{I2cStatus::NotFound};
    I2cStatus recovery_status{I2cStatus::Ok};
    bool failure_consumed{};
    bool spad_ready{true};
    bool calibration_ready{true};
    bool measurement_start_cleared{true};
    bool measurement_ready{true};
    std::array<std::uint8_t, 2U> initial_rate_limit{};
    std::uint32_t initial_rate_limit_writes{};

    [[nodiscard]] auto resolve(const std::uint8_t address) const noexcept -> std::size_t {
        std::size_t found = vl53::kSensorCount;
        for (std::size_t index = 0U; index < vl53::kSensorCount; ++index) {
            if (hardware_.enabled[index] && hardware_.address[index] == address) {
                if (found != vl53::kSensorCount) return vl53::kSensorCount;
                found = index;
            }
        }
        return found;
    }

    [[nodiscard]] auto transfer(const I2cTransaction& transaction) noexcept
        -> I2cStatus override {
        ++operations;
        if (!failure_consumed && fail_operation != 0U && operations == fail_operation) {
            failure_consumed = true;
            return failure_status;
        }
        const auto instance = resolve(transaction.seven_bit_address);
        if (instance >= vl53::kSensorCount || transaction.write.size == 0U) {
            return I2cStatus::NotFound;
        }
        const auto reg = transaction.write.data[0];
        if (transaction.read.size == 0U) {
            if (transaction.write.size < 2U) return I2cStatus::InvalidArgument;
            if (reg == 0x8AU) {
                hardware_.address[instance] = transaction.write.data[1];
            } else if (reg == 0x91U && transaction.write.size == 2U) {
                hardware_.restored_stop_variable[instance] = transaction.write.data[1];
            } else if (reg == 0x44U && transaction.write.size == 3U) {
                initial_rate_limit = {{transaction.write.data[1],
                                       transaction.write.data[2]}};
                ++initial_rate_limit_writes;
            }
            return I2cStatus::Ok;
        }
        if (transaction.read.data == nullptr) return I2cStatus::InvalidArgument;
        for (std::size_t index = 0U; index < transaction.read.size; ++index) {
            transaction.read.data[index] = 0U;
        }
        if (reg == 0xC0U && transaction.read.size == 2U) {
            transaction.read.data[0] = 0xEEU;
            transaction.read.data[1] = 0xAAU;
        } else if (reg == 0x83U && transaction.read.size == 1U) {
            transaction.read.data[0] = spad_ready ? 1U : 0U;
        } else if (reg == 0x92U && transaction.read.size == 1U) {
            transaction.read.data[0] = 0x85U;  // five aperture SPADs
        } else if (reg == 0x91U && transaction.read.size == 1U) {
            transaction.read.data[0] = hardware_.stop_variable[instance];
        } else if (reg == 0xB0U && transaction.read.size == 6U) {
            for (std::size_t index = 0U; index < 6U; ++index) {
                transaction.read.data[index] = 0xFFU;
            }
        } else if (reg == 0x00U && transaction.read.size == 1U) {
            transaction.read.data[0] = measurement_start_cleared ? 0U : 1U;
        } else if (reg == 0x13U && transaction.read.size == 1U) {
            transaction.read.data[0] =
                (calibration_ready && measurement_ready) ? 0x07U : 0U;
        } else if (reg == 0x14U && transaction.read.size == 12U) {
            transaction.read.data[0] =
                static_cast<std::uint8_t>(hardware_.range_status[instance] << 3U);
            transaction.read.data[10] =
                static_cast<std::uint8_t>(hardware_.distance_mm[instance] >> 8U);
            transaction.read.data[11] =
                static_cast<std::uint8_t>(hardware_.distance_mm[instance] & 0xFFU);
        }
        return I2cStatus::Ok;
    }

    [[nodiscard]] auto recover(std::uint32_t timeout_us) noexcept
        -> I2cStatus override {
        ++operations;
        ++recoveries;
        return timeout_us == 0U ? I2cStatus::InvalidArgument : recovery_status;
    }
};

struct Publisher final : shahbaz::interfaces::ISamplePublisher {
    std::vector<shahbaz::domain::SensorSample> samples;
    bool accept{true};

    [[nodiscard]] auto publish(
        const shahbaz::domain::SensorSample& sample) noexcept -> bool override {
        if (!accept) return false;
        samples.push_back(sample);
        return true;
    }
};

struct Fixture final {
    FakeHardware hardware{};
    FakeClock clock{};
    FakeShutdown shutdown{hardware};
    FakeBus bus{hardware};
    Publisher publisher{};
};

void advance(FakeClock& clock, const vl53::ArrayDriver& driver) {
    const auto deadline = driver.next_deadline_us();
    if (deadline > clock.now && deadline != std::numeric_limits<std::uint64_t>::max()) {
        clock.now = deadline;
    } else if (clock.now != std::numeric_limits<std::uint64_t>::max()) {
        ++clock.now;
    }
}

auto run_until_samples(Fixture& fixture, vl53::ArrayDriver& driver,
                       const std::size_t count,
                       const std::size_t maximum_steps = 20'000U) -> bool {
    for (std::size_t step = 0U; step < maximum_steps; ++step) {
        (void)driver.step();
        if (fixture.publisher.samples.size() >= count) return true;
        advance(fixture.clock, driver);
    }
    return false;
}

auto enabled_config() -> vl53::ArrayConfig {
    auto config = vl53::ArrayConfig{};
    config.enabled = true;
    config.shutdown_settle_us = 10U;
    config.boot_wait_us = 10U;
    config.initialization_poll_interval_us = 10U;
    config.initialization_timeout_us = 100U;
    config.measurement_poll_interval_us = 10U;
    config.measurement_timeout_us = vl53::kMinimumTimingBudgetUs;
    config.per_sensor_sample_interval_us = vl53::kMinimumTimingBudgetUs;
    config.retry_initial_backoff_us = 10U;
    config.retry_maximum_backoff_us = 100U;
    return config;
}

}  // namespace

int main() {
    int failures = 0;

    {
        Fixture fixture;
        vl53::ArrayConfig config{};
        vl53::ArrayDriver driver(fixture.bus, fixture.clock, fixture.publisher,
                                 fixture.shutdown, config);
        failures += expect(driver.state() == vl53::ArrayState::Disabled,
                           "feature is fail-closed by default");
        failures += expect(driver.lifecycle(0U) == vl53::Lifecycle::DisabledOrAbsent,
                           "disabled lifecycle never claims fitted hardware");
        failures += expect(!driver.ready_at(0U), "disabled driver never schedules bus work");
        failures += expect(fixture.shutdown.operations == 0U && fixture.bus.operations == 0U,
                           "disabled driver does not touch hardware");
    }

    {
        Fixture fixture;
        auto config = enabled_config();
        config.assigned_addresses[1] = config.assigned_addresses[0];
        vl53::ArrayDriver driver(fixture.bus, fixture.clock, fixture.publisher,
                                 fixture.shutdown, config);
        failures += expect(driver.state() == vl53::ArrayState::Fault,
                           "duplicate assigned addresses fail construction");
        failures += expect(driver.lifecycle(0U) == vl53::Lifecycle::Degraded,
                           "invalid configured array reports degraded");
        failures += expect(driver.lifecycle(vl53::kSensorCount) ==
                               vl53::Lifecycle::Degraded,
                           "out-of-range lifecycle query fails closed");
        failures += expect(!driver.health(0U).configuration_valid,
                           "invalid configuration is observable per device");
    }

    {
        Fixture fixture;
        WrongSizeShutdown wrong_shutdown;
        auto config = enabled_config();
        vl53::ArrayDriver driver(fixture.bus, fixture.clock, fixture.publisher,
                                 wrong_shutdown, config);
        failures += expect(driver.state() == vl53::ArrayState::Fault,
                           "XSHUT bank must expose exactly four controls");
    }

    {
        Fixture fixture;
        for (auto& enabled : fixture.hardware.enabled) enabled = true;
        auto config = enabled_config();
        vl53::ArrayDriver driver(fixture.bus, fixture.clock, fixture.publisher,
                                 fixture.shutdown, config);
        driver.invalidate_after_shared_recovery(50U);
        failures += expect(driver.state() == vl53::ArrayState::HoldAllShutdown &&
                               !driver.ready_at(49U),
                           "shared recovery cannot bypass the initial all-XSHUT hold");
        fixture.clock.now = 50U;
        const auto step = driver.step();
        failures += expect(step.shutdown_operation,
                           "post-recovery startup performs the required bulk shutdown");
        bool all_disabled = true;
        for (const auto enabled : fixture.hardware.enabled) {
            all_disabled = all_disabled && !enabled;
        }
        failures += expect(all_disabled,
                           "initial recovery barrier leaves no default-address peer active");
    }

    {
        Fixture fixture;
        auto config = enabled_config();
        vl53::ArrayDriver driver(fixture.bus, fixture.clock, fixture.publisher,
                                 fixture.shutdown, config);
        (void)driver.step();
        fixture.clock.now = driver.next_deadline_us();
        (void)driver.step();
        (void)driver.step();
        (void)driver.step();
        const auto interrupted = static_cast<std::size_t>(driver.current_instance());
        failures += expect(driver.state() == vl53::ArrayState::WaitForBoot &&
                               fixture.hardware.enabled[interrupted],
                           "fixture reaches the active default-address boot window");
        driver.invalidate_after_shared_recovery(fixture.clock.now + 50U);
        failures += expect(!fixture.hardware.enabled[interrupted] &&
                               driver.state() == vl53::ArrayState::RecoverySettle,
                           "shared recovery isolates a device interrupted before readdressing");
        fixture.clock.now = driver.next_deadline_us();
        (void)driver.step();
        failures += expect(driver.state() == vl53::ArrayState::HoldAllShutdown,
                           "recovery settles into a full address-bank restart");
    }

    std::uint32_t happy_operations_to_first_sample = 0U;
    {
        Fixture fixture;
        auto config = enabled_config();
        vl53::ArrayDriver driver(fixture.bus, fixture.clock, fixture.publisher,
                                 fixture.shutdown, config);
        failures += expect(driver.lifecycle(0U) == vl53::Lifecycle::Initializing,
                           "enabled role reports initializing before first valid sample");
        failures += expect(run_until_samples(fixture, driver, 4U),
                           "four-sensor happy path initializes and publishes");
        happy_operations_to_first_sample = fixture.bus.operations;
        for (std::size_t instance = 0U; instance < vl53::kSensorCount; ++instance) {
            failures += expect(driver.health(instance).online,
                               "every configured sensor becomes independently online");
            failures += expect(driver.lifecycle(instance) == vl53::Lifecycle::Live,
                               "every healthy initialized role reports live");
            failures += expect(fixture.hardware.enabled[instance],
                               "every initialized sensor is released from XSHUT");
            failures += expect(fixture.hardware.address[instance] ==
                                   vl53::kDefaultAssignedAddresses[instance],
                               "every sensor receives its stable unique address");
            failures += expect(
                fixture.hardware.restored_stop_variable[instance] ==
                    fixture.hardware.stop_variable[instance],
                "each sensor restores its own device-specific stop variable");
            failures += expect(fixture.publisher.samples[instance].instance_id == instance,
                               "first publication order is fair and role-stable");
            failures += expect(
                fixture.publisher.samples[instance].sensor_id ==
                    shahbaz::domain::SensorId::Vl53l0x,
                "published sensor ID is the protocol allocation");
            failures += expect(fixture.publisher.samples[instance].field_count == 3U,
                               "distance/status/quality are published atomically");
            failures += expect(fixture.publisher.samples[instance].health_flags == 0U,
                               "valid reading has no health fault bits");
        }
        failures += expect(
            fixture.bus.initial_rate_limit_writes == vl53::kSensorCount &&
                fixture.bus.initial_rate_limit[0] == 0x00U &&
                fixture.bus.initial_rate_limit[1] == 0x20U,
            "0.25 MCPS signal-rate limit uses the sensor's Q9.7 encoding");
        failures += expect(!driver.set_sample_interval_us(
                               config.measurement_timeout_us - 1U),
                           "sample interval cannot undercut measurement timeout");
        failures += expect(driver.set_sample_interval_us(
                               config.measurement_timeout_us + 1U),
                           "safe sample interval can be updated without reinitialization");
    }

    {
        Fixture fixture;
        auto config = enabled_config();
        vl53::ArrayDriver driver(fixture.bus, fixture.clock, fixture.publisher,
                                 fixture.shutdown, config);
        failures += expect(run_until_samples(fixture, driver, 4U),
                           "fixture reaches a fully addressed array before shared recovery");
        driver.invalidate_after_shared_recovery(fixture.clock.now + 50U);
        bool no_role_still_live = true;
        for (std::size_t instance = 0U; instance < vl53::kSensorCount; ++instance) {
            no_role_still_live = no_role_still_live &&
                driver.lifecycle(instance) != vl53::Lifecycle::Live;
        }
        failures += expect(no_role_still_live,
                           "recovery invalidation immediately withdraws LIVE lifecycle claims");
        fixture.clock.now = driver.next_deadline_us();
        (void)driver.step();
        (void)driver.step();
        bool all_reset = true;
        for (std::size_t instance = 0U; instance < vl53::kSensorCount; ++instance) {
            all_reset = all_reset && !fixture.hardware.enabled[instance] &&
                        fixture.hardware.address[instance] == vl53::kDefaultAddress &&
                        !driver.health(instance).online &&
                        driver.health(instance).shared_bus_invalidations == 1U;
        }
        failures += expect(all_reset,
                           "shared recovery resets every XSHUT line and volatile address");
        failures += expect(run_until_samples(fixture, driver, 8U, 40'000U),
                           "all four roles are readdressed after shared recovery");
    }

    // Every I2C operation on a complete initialization/first-sample path is
    // failed once with NotFound. The isolated device must retry without a hang
    // and all four sensors must eventually publish.
    for (std::uint32_t operation = 1U;
         operation <= happy_operations_to_first_sample;
         ++operation) {
        Fixture fixture;
        fixture.bus.fail_operation = operation;
        fixture.bus.failure_status = I2cStatus::NotFound;
        auto config = enabled_config();
        vl53::ArrayDriver driver(fixture.bus, fixture.clock, fixture.publisher,
                                 fixture.shutdown, config);
        if (!run_until_samples(fixture, driver, 4U, 40'000U)) {
            std::cerr << "FAIL: one-shot NotFound did not recover at operation "
                      << operation << '\n';
            ++failures;
            break;
        }
    }

    {
        Fixture fixture;
        fixture.bus.fail_operation = 1U;
        fixture.bus.failure_status = I2cStatus::NotFound;
        auto config = enabled_config();
        vl53::ArrayDriver driver(fixture.bus, fixture.clock, fixture.publisher,
                                 fixture.shutdown, config);
        failures += expect(run_until_samples(fixture, driver, 4U),
                           "isolated device retry eventually publishes");
        bool recovered_sample_found = false;
        for (const auto& sample : fixture.publisher.samples) {
            if (sample.instance_id != 0U) continue;
            recovered_sample_found =
                (static_cast<std::uint32_t>(sample.quality) &
                 static_cast<std::uint32_t>(
                     shahbaz::domain::QualityFlag::RecoveredAfterError)) != 0U;
            break;
        }
        failures += expect(recovered_sample_found,
                           "first successful sample after retry is marked recovered");
    }

    {
        Fixture fixture;
        fixture.bus.fail_operation = 1U;
        fixture.bus.failure_status = I2cStatus::Timeout;
        auto config = enabled_config();
        vl53::ArrayDriver driver(fixture.bus, fixture.clock, fixture.publisher,
                                 fixture.shutdown, config);
        failures += expect(run_until_samples(fixture, driver, 1U),
                           "shared bus timeout recovers and resumes");
        failures += expect(fixture.bus.recoveries == 1U,
                           "shared electrical fault invokes one bounded recovery");
    }

    {
        Fixture fixture;
        fixture.bus.fail_operation = 1U;
        fixture.bus.failure_status = I2cStatus::Timeout;
        fixture.bus.recovery_status = I2cStatus::BusError;
        auto config = enabled_config();
        vl53::ArrayDriver driver(fixture.bus, fixture.clock, fixture.publisher,
                                 fixture.shutdown, config);
        (void)driver.step();  // initial all-XSHUT hold
        advance(fixture.clock, driver);
        (void)driver.step();  // enter work selection
        (void)driver.step();  // select first device
        (void)driver.step();  // release first device
        advance(fixture.clock, driver);
        (void)driver.step();  // wait-for-boot -> identity read
        (void)driver.step();  // injected timeout -> RecoverBus
        const auto recovery_at = fixture.clock.now;
        const auto recovery = driver.step();
        failures += expect(
            recovery.bus_settle_until_us > recovery_at,
            "failed shared recovery still publishes an electrical settle barrier");
        failures += expect(
            driver.state() == vl53::ArrayState::HoldAllShutdown &&
                !driver.ready_at(recovery_at),
            "failed shared recovery delays the mandatory full XSHUT restart");
        fixture.clock.now = recovery.bus_settle_until_us;
        const auto hold = driver.step();
        failures += expect(hold.shutdown_operation,
                           "failed shared recovery resumes by holding the full array down");
    }

    {
        Fixture fixture;
        fixture.shutdown.fail_disable_all = true;
        auto config = enabled_config();
        vl53::ArrayDriver driver(fixture.bus, fixture.clock, fixture.publisher,
                                 fixture.shutdown, config);
        (void)driver.step();
        failures += expect(driver.state() == vl53::ArrayState::Fault,
                           "failure to assert all XSHUT lines fails closed");
        failures += expect(driver.lifecycle(0U) == vl53::Lifecycle::Degraded,
                           "runtime XSHUT failure reports degraded");
        failures += expect(fixture.bus.operations == 0U,
                           "XSHUT setup failure cannot reach I2C");
    }

    {
        Fixture fixture;
        fixture.bus.spad_ready = false;
        auto config = enabled_config();
        config.initialization_timeout_us = 20U;
        vl53::ArrayDriver driver(fixture.bus, fixture.clock, fixture.publisher,
                                 fixture.shutdown, config);
        for (std::size_t step = 0U; step < 1'000U; ++step) {
            (void)driver.step();
            advance(fixture.clock, driver);
            bool timed_out = false;
            for (std::size_t instance = 0U; instance < vl53::kSensorCount; ++instance) {
                timed_out = timed_out ||
                    driver.health(instance).initialization_timeouts != 0U;
            }
            if (timed_out) break;
        }
        bool timed_out = false;
        for (std::size_t instance = 0U; instance < vl53::kSensorCount; ++instance) {
            timed_out = timed_out || driver.health(instance).initialization_timeouts != 0U;
        }
        failures += expect(timed_out, "SPAD readiness poll has a hard deadline");
    }

    {
        Fixture fixture;
        auto config = enabled_config();
        vl53::ArrayDriver driver(fixture.bus, fixture.clock, fixture.publisher,
                                 fixture.shutdown, config);
        failures += expect(run_until_samples(fixture, driver, 4U),
                           "fixture reaches steady ranging before invalid-result test");
        fixture.hardware.range_status[0] = 2U;
        fixture.hardware.distance_mm[0] = 0U;
        failures += expect(run_until_samples(fixture, driver, 8U),
                           "invalid range status is still observable telemetry");
        const auto& invalid = fixture.publisher.samples[4U];
        failures += expect(invalid.instance_id == 0U && invalid.health_flags == 3U,
                           "invalid status and unusable distance are explicit health bits");
        failures += expect(driver.health(0U).invalid_measurements == 1U,
                           "invalid measurement counter increments without offlining sensor");
        failures += expect(driver.health(0U).online,
                           "optical invalidity does not masquerade as transport loss");
    }

    {
        Fixture fixture;
        fixture.publisher.accept = false;
        auto config = enabled_config();
        vl53::ArrayDriver driver(fixture.bus, fixture.clock, fixture.publisher,
                                 fixture.shutdown, config);
        for (std::size_t step = 0U; step < 10'000U; ++step) {
            (void)driver.step();
            advance(fixture.clock, driver);
            if (driver.health(0U).publication_drops != 0U) break;
        }
        failures += expect(driver.health(0U).publication_drops != 0U,
                           "publisher backpressure is counted and bounded");
        failures += expect(driver.health(0U).online,
                           "telemetry backpressure does not reset healthy ranging hardware");
        fixture.publisher.accept = true;
        failures += expect(run_until_samples(fixture, driver, 1U),
                           "publisher recovery allows sampling to resume");
        failures += expect(driver.health(fixture.publisher.samples[0U].instance_id).last_error ==
                               vl53::DriverError::None,
                           "successful publication clears transient backpressure status");
    }

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
