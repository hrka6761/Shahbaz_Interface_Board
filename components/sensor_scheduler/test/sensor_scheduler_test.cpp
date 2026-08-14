/**
 * @file sensor_scheduler_test.cpp
 * @brief Fixed-capacity host fakes and deterministic scheduler tests.
 */
#include "shahbaz/sensors/sensor_scheduler.hpp"

#include "shahbaz/domain/measurement.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace {

using shahbaz::domain::QualityFlag;
using shahbaz::domain::SensorId;
using shahbaz::domain::SensorSample;
using shahbaz::interfaces::I2cStatus;
using shahbaz::interfaces::I2cTransaction;
using shahbaz::interfaces::I2cWriteReadBoundary;
using shahbaz::sensors::scheduler::DriverError;
using shahbaz::sensors::scheduler::Ms5611Config;
using shahbaz::sensors::scheduler::Ms5611State;
using shahbaz::sensors::scheduler::Ms5611StateMachine;
using shahbaz::sensors::scheduler::SharedSensorScheduler;
using shahbaz::sensors::scheduler::Sht3xConfig;
using shahbaz::sensors::scheduler::Sht3xState;
using shahbaz::sensors::scheduler::Sht3xStateMachine;

int failures = 0;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                      \
            std::fprintf(stderr, "%s:%d check failed: %s\n", __FILE__,         \
                         __LINE__, #condition);                                  \
            ++failures;                                                         \
        }                                                                       \
    } while (false)

class FakeClock final : public shahbaz::interfaces::IMonotonicClock {
  public:
    [[nodiscard]] auto now_us() const noexcept -> std::uint64_t override {
        return now_us_;
    }

    void set(const std::uint64_t value) noexcept { now_us_ = value; }
    void advance(const std::uint64_t delta) noexcept { now_us_ += delta; }

  private:
    std::uint64_t now_us_{};
};

struct BusOperation final {
    bool recovery{};
    std::uint8_t address{};
    std::uint16_t command{0xFFFFU};
    std::size_t write_size{};
    std::size_t read_size{};
    std::uint32_t timeout_us{};
    std::uint64_t started_at_us{};
    I2cWriteReadBoundary write_read_boundary{I2cWriteReadBoundary::StopThenStart};
};

class FakeBus final : public shahbaz::interfaces::II2cBus {
  public:
    static constexpr std::size_t kMaximumOperations = 1'024U;

    explicit FakeBus(FakeClock& clock) noexcept : clock_(clock) {}

    [[nodiscard]] auto transfer(const I2cTransaction& transaction) noexcept
        -> I2cStatus override {
        record(transaction);
        ++transfer_count;
        I2cStatus result = I2cStatus::Ok;
        if ((transaction.write.size != 0U && transaction.write.data == nullptr) ||
            (transaction.read.size != 0U && transaction.read.data == nullptr) ||
            transaction.timeout_us == 0U) {
            result = I2cStatus::InvalidArgument;
        } else if (transaction.seven_bit_address == 0x44U) {
            if (fail_sht_always || fail_next_sht_transfers != 0U) {
                if (fail_next_sht_transfers != 0U) {
                    --fail_next_sht_transfers;
                }
                result = I2cStatus::Timeout;
            } else {
                result = handle_sht(transaction);
            }
        } else if (transaction.seven_bit_address == 0x77U) {
            if (fail_next_ms_transfers != 0U) {
                --fail_next_ms_transfers;
                result = I2cStatus::BusError;
            } else {
                result = handle_ms(transaction);
            }
        } else {
            result = I2cStatus::NotFound;
        }
        clock_.advance(operation_duration_us);
        return result;
    }

    [[nodiscard]] auto recover(const std::uint32_t timeout_us) noexcept
        -> I2cStatus override {
        if (operation_count < operations.size()) {
            operations[operation_count] =
                {true, 0U, 0xFFFFU, 0U, 0U, timeout_us, clock_.now_us()};
        }
        ++operation_count;
        ++recovery_count;
        clock_.advance(operation_duration_us);
        return recovery_status;
    }

    [[nodiscard]] auto operation_at(const std::size_t index) const noexcept
        -> const BusOperation& {
        return operations[index];
    }

    bool fail_sht_always{};
    std::uint32_t fail_next_sht_transfers{};
    std::uint32_t fail_next_ms_transfers{};
    std::uint32_t corrupt_sht_measurements{};
    std::uint32_t corrupt_ms_prom_cycles{};
    std::uint16_t sht_status_word{};
    I2cStatus recovery_status{I2cStatus::Ok};
    std::uint32_t operation_duration_us{};
    std::array<BusOperation, kMaximumOperations> operations{};
    std::size_t operation_count{};
    std::size_t transfer_count{};
    std::size_t recovery_count{};

  private:
    enum class PendingConversion : std::uint8_t {
        None,
        PressureD1,
        TemperatureD2,
    };

    static constexpr std::array<std::uint16_t, 8> kProm{
        0xA5A5U, 40'127U, 36'924U, 23'317U,
        23'282U, 33'464U, 28'312U, 0x0005U,
    };

    void record(const I2cTransaction& transaction) noexcept {
        std::uint16_t command = 0xFFFFU;
        if (transaction.write.size == 1U && transaction.write.data != nullptr) {
            command = transaction.write.data[0];
        } else if (transaction.write.size == 2U &&
                   transaction.write.data != nullptr) {
            command = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(transaction.write.data[0]) << 8U) |
                transaction.write.data[1]);
        }
        if (operation_count < operations.size()) {
            operations[operation_count] = {
                false,
                transaction.seven_bit_address,
                command,
                transaction.write.size,
                transaction.read.size,
                transaction.timeout_us,
                clock_.now_us(),
                transaction.write_read_boundary,
            };
        }
        ++operation_count;
    }

    [[nodiscard]] static auto write_bytes(
        const I2cTransaction& transaction, const std::uint8_t* source,
        const std::size_t size) noexcept -> I2cStatus {
        if (transaction.read.size != size || transaction.read.data == nullptr) {
            return I2cStatus::InvalidArgument;
        }
        for (std::size_t index = 0U; index < size; ++index) {
            transaction.read.data[index] = source[index];
        }
        return I2cStatus::Ok;
    }

    [[nodiscard]] auto handle_sht(const I2cTransaction& transaction) noexcept
        -> I2cStatus {
        if (transaction.write.size == 2U && transaction.read.size == 0U) {
            const auto command = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(transaction.write.data[0]) << 8U) |
                transaction.write.data[1]);
            if (command == 0x3041U) {
                constexpr std::uint16_t kClearableStatusMask =
                    static_cast<std::uint16_t>((1U << 15U) | (1U << 11U) |
                                               (1U << 10U) | (1U << 4U));
                sht_status_word = static_cast<std::uint16_t>(
                    sht_status_word &
                    static_cast<std::uint16_t>(~kClearableStatusMask));
            }
            return I2cStatus::Ok;
        }
        if (transaction.write.size == 2U && transaction.read.size == 3U &&
            transaction.write.data[0] == 0xF3U &&
            transaction.write.data[1] == 0x2DU) {
            std::array<std::uint8_t, 3> status{
                static_cast<std::uint8_t>(sht_status_word >> 8U),
                static_cast<std::uint8_t>(sht_status_word & 0x00FFU), 0U};
            status[2] = shahbaz::sensors::sht3x::crc8(status.data(), 2U);
            return write_bytes(transaction, status.data(), status.size());
        }
        if (transaction.write.size == 0U && transaction.read.size == 6U) {
            std::array<std::uint8_t, 6> sample{
                0xBEU, 0xEFU, 0x92U, 0x00U, 0x00U, 0x81U};
            if (corrupt_sht_measurements != 0U) {
                --corrupt_sht_measurements;
                sample[2] ^= 0x01U;
            }
            return write_bytes(transaction, sample.data(), sample.size());
        }
        return I2cStatus::InvalidArgument;
    }

    [[nodiscard]] auto handle_ms(const I2cTransaction& transaction) noexcept
        -> I2cStatus {
        if (transaction.write.size != 1U || transaction.write.data == nullptr) {
            return I2cStatus::InvalidArgument;
        }
        const auto command = transaction.write.data[0];
        if (transaction.read.size == 0U) {
            if (command == 0x1EU) {
                pending_ = PendingConversion::None;
                return I2cStatus::Ok;
            }
            if ((command & 0xF0U) == 0x40U) {
                pending_ = PendingConversion::PressureD1;
                return I2cStatus::Ok;
            }
            if ((command & 0xF0U) == 0x50U) {
                pending_ = PendingConversion::TemperatureD2;
                return I2cStatus::Ok;
            }
            return I2cStatus::InvalidArgument;
        }
        if (transaction.read.size == 2U && command >= 0xA0U &&
            command <= 0xAEU && (command & 1U) == 0U) {
            const auto index = static_cast<std::size_t>((command - 0xA0U) / 2U);
            auto word = kProm[index];
            if (index == 7U && corrupt_ms_prom_cycles != 0U) {
                --corrupt_ms_prom_cycles;
                word = static_cast<std::uint16_t>(word ^ 0x0001U);
            }
            const std::array<std::uint8_t, 2> bytes{
                static_cast<std::uint8_t>(word >> 8U),
                static_cast<std::uint8_t>(word & 0x00FFU),
            };
            return write_bytes(transaction, bytes.data(), bytes.size());
        }
        if (transaction.read.size == 3U && command == 0x00U) {
            std::array<std::uint8_t, 3> bytes{};
            if (pending_ == PendingConversion::PressureD1) {
                bytes = {0x8AU, 0xA2U, 0x1AU};
            } else if (pending_ == PendingConversion::TemperatureD2) {
                bytes = {0x82U, 0xC1U, 0x3EU};
            }
            pending_ = PendingConversion::None;
            return write_bytes(transaction, bytes.data(), bytes.size());
        }
        return I2cStatus::InvalidArgument;
    }

    PendingConversion pending_{PendingConversion::None};
    FakeClock& clock_;
};

class FakePublisher final : public shahbaz::interfaces::ISamplePublisher {
  public:
    static constexpr std::size_t kMaximumSamples = 128U;

    [[nodiscard]] auto publish(const SensorSample& sample) noexcept
        -> bool override {
        if (reject_next != 0U) {
            --reject_next;
            return false;
        }
        if (sample_count >= samples.size()) {
            return false;
        }
        samples[sample_count++] = sample;
        return true;
    }

    [[nodiscard]] auto count_for(const SensorId id) const noexcept
        -> std::size_t {
        std::size_t count = 0U;
        for (std::size_t index = 0U; index < sample_count; ++index) {
            if (samples[index].sensor_id == id) {
                ++count;
            }
        }
        return count;
    }

    [[nodiscard]] auto first_for(const SensorId id) const noexcept
        -> const SensorSample* {
        for (std::size_t index = 0U; index < sample_count; ++index) {
            if (samples[index].sensor_id == id) {
                return &samples[index];
            }
        }
        return nullptr;
    }

    std::array<SensorSample, kMaximumSamples> samples{};
    std::size_t sample_count{};
    std::uint32_t reject_next{};
};

void drive(SharedSensorScheduler& scheduler, FakeClock& clock, FakeBus& bus,
           const std::size_t steps, const std::uint64_t advance_us = 1'000U) {
    for (std::size_t index = 0U; index < steps; ++index) {
        const auto before = bus.operation_count;
        const auto result = scheduler.step();
        const auto operations = bus.operation_count - before;
        CHECK(operations <= 1U);
        CHECK(!result.step.bus_operation || operations == 1U);
        clock.advance(advance_us);
    }
}

[[nodiscard]] auto has_quality(const SensorSample& sample,
                               const QualityFlag flag) noexcept -> bool {
    return (static_cast<std::uint32_t>(sample.quality) &
            static_cast<std::uint32_t>(flag)) != 0U;
}

void test_nominal_shared_schedule_and_order() {
    FakeClock clock;
    FakeBus bus(clock);
    FakePublisher publisher;
    Sht3xConfig sht_config{};
    sht_config.sample_interval_us = 100'000U;
    Ms5611Config ms_config{};
    ms_config.temperature_interval_us = 100'000U;
    ms_config.maximum_temperature_age_us = 200'000U;
    SharedSensorScheduler scheduler(bus, clock, publisher, sht_config,
                                    ms_config);

    drive(scheduler, clock, bus, 800U);

    CHECK(publisher.count_for(SensorId::Sht3x) >= 2U);
    CHECK(publisher.count_for(SensorId::Ms5611) >= 2U);
    CHECK(scheduler.sht3x().health().online);
    CHECK(scheduler.ms5611().health().online);
    CHECK(scheduler.ms5611().last_pressure_timestamp_us() != 0U);
    CHECK(scheduler.ms5611().last_temperature_timestamp_us() != 0U);

    const auto* sht_sample = publisher.first_for(SensorId::Sht3x);
    const auto* ms_sample = publisher.first_for(SensorId::Ms5611);
    CHECK(sht_sample != nullptr);
    CHECK(ms_sample != nullptr);
    if (sht_sample != nullptr) {
        CHECK(sht_sample->field_count == 2U);
        CHECK(sht_sample->fields[0].value == 85'523);
        CHECK(sht_sample->fields[1].value == 0);
    }
    if (ms_sample != nullptr) {
        CHECK(ms_sample->field_count == 2U);
        CHECK(ms_sample->fields[0].value == 100'009);
        CHECK(ms_sample->fields[1].value == 20'070);
    }

    constexpr std::array<std::uint16_t, 5> expected_sht{
        0x30A2U, 0xF32DU, 0x3041U, 0x2400U, 0xFFFFU};
    constexpr std::array<std::uint16_t, 13> expected_ms{
        0x001EU, 0x00A0U, 0x00A2U, 0x00A4U, 0x00A6U,
        0x00A8U, 0x00AAU, 0x00ACU, 0x00AEU, 0x0058U,
        0x0000U, 0x0048U, 0x0000U};
    std::size_t sht_index = 0U;
    std::size_t ms_index = 0U;
    for (std::size_t index = 0U; index < bus.operation_count; ++index) {
        const auto& operation = bus.operation_at(index);
        if (!operation.recovery && operation.write_size != 0U &&
            operation.read_size != 0U) {
            CHECK(operation.write_read_boundary ==
                  I2cWriteReadBoundary::StopThenStart);
        }
        if (!operation.recovery && operation.address == 0x44U &&
            sht_index < expected_sht.size()) {
            CHECK(operation.command == expected_sht[sht_index]);
            ++sht_index;
        }
        if (!operation.recovery && operation.address == 0x77U &&
            ms_index < expected_ms.size()) {
            CHECK(operation.command == expected_ms[ms_index]);
            ++ms_index;
        }
    }
    CHECK(sht_index == expected_sht.size());
    CHECK(ms_index == expected_ms.size());
}

void test_sht_deadlines_do_not_read_or_wait_early() {
    FakeClock clock;
    FakeBus bus(clock);
    FakePublisher publisher;
    Sht3xStateMachine machine(bus, clock, publisher);

    CHECK(!machine.step().state_advanced);
    CHECK(bus.operation_count == 0U);
    clock.set(2'000U);
    CHECK(machine.step().state_advanced);
    CHECK(machine.state() == Sht3xState::SendSoftReset);
    CHECK(bus.operation_count == 0U);
    CHECK(machine.step().bus_operation);
    CHECK(machine.state() == Sht3xState::WaitAfterReset);
    CHECK(bus.operation_count == 1U);

    CHECK(!machine.step().state_advanced);
    clock.set(3'999U);
    CHECK(!machine.step().state_advanced);
    CHECK(bus.operation_count == 1U);
    clock.set(4'000U);
    CHECK(machine.step().state_advanced);
    CHECK(machine.state() == Sht3xState::ReadStatus);
    CHECK(bus.operation_count == 1U);
    CHECK(machine.step().bus_operation);
    CHECK(bus.operation_count == 2U);
}

void test_sht_latched_status_is_retained_and_only_documented_flags_clear() {
    FakeClock clock;
    FakeBus bus(clock);
    FakePublisher publisher;
    bus.sht_status_word = 0x8C13U;
    Sht3xStateMachine machine(bus, clock, publisher);

    for (std::size_t index = 0U; index < 64U &&
                                publisher.sample_count == 0U;
         ++index) {
        if (!machine.ready_at(clock.now_us())) {
            clock.set(machine.next_deadline_us());
        }
        (void)machine.step();
    }

    CHECK(publisher.sample_count == 1U);
    CHECK(machine.health().sensor_status_warnings == 1U);
    CHECK(machine.health().total_failures == 0U);
    CHECK(machine.has_status());
    CHECK(machine.last_status().last_command_failed);
    CHECK(machine.last_status().last_write_checksum_failed);
    CHECK(bus.sht_status_word == 0x0003U);
    CHECK(bus.recovery_count == 0U);
}

void test_sht_post_reset_self_test_is_always_high_repeatability() {
    FakeClock clock;
    FakeBus bus(clock);
    FakePublisher publisher;
    Sht3xConfig config{};
    config.repeatability = shahbaz::sensors::sht3x::Repeatability::Low;
    config.sample_interval_us = 20'000U;
    Sht3xStateMachine machine(bus, clock, publisher, config);
    std::array<std::uint16_t, 2> measurement_commands{};
    std::size_t command_count = 0U;

    for (std::size_t step = 0U;
         step < 128U && command_count < measurement_commands.size(); ++step) {
        if (!machine.ready_at(clock.now_us())) {
            clock.set(machine.next_deadline_us());
        }
        const auto before = bus.operation_count;
        (void)machine.step();
        for (std::size_t index = before; index < bus.operation_count; ++index) {
            const auto command = bus.operation_at(index).command;
            if (command == 0x2400U || command == 0x2416U) {
                measurement_commands[command_count++] = command;
            }
        }
    }

    CHECK(command_count == measurement_commands.size());
    if (command_count == measurement_commands.size()) {
        CHECK(measurement_commands[0] == 0x2400U);
        CHECK(measurement_commands[1] == 0x2416U);
    }
}

void test_ms_adc_deadline_is_enforced() {
    FakeClock clock;
    FakeBus bus(clock);
    FakePublisher publisher;
    Ms5611StateMachine machine(bus, clock, publisher);

    for (std::size_t index = 0U; index < 64U &&
                                machine.state() !=
                                    Ms5611State::WaitForTemperatureD2;
         ++index) {
        if (!machine.ready_at(clock.now_us())) {
            clock.set(machine.next_deadline_us());
        }
        (void)machine.step();
    }
    CHECK(machine.state() == Ms5611State::WaitForTemperatureD2);
    const auto before = bus.operation_count;
    CHECK(!machine.step().state_advanced);
    CHECK(bus.operation_count == before);
    const auto deadline = machine.next_deadline_us();
    clock.set(deadline - 1U);
    CHECK(!machine.step().state_advanced);
    CHECK(bus.operation_count == before);
    clock.set(deadline);
    CHECK(machine.step().state_advanced);
    CHECK(machine.state() == Ms5611State::ReadTemperatureD2);
    CHECK(bus.operation_count == before);
    CHECK(machine.step().bus_operation);
    CHECK(bus.operation_count == before + 1U);
}

void test_deadline_starts_after_bounded_transfer_completion() {
    FakeClock clock;
    FakeBus bus(clock);
    FakePublisher publisher;
    bus.operation_duration_us = 750U;
    Sht3xStateMachine machine(bus, clock, publisher);

    clock.set(2'000U);
    CHECK(machine.step().state_advanced);
    CHECK(machine.step().bus_operation);
    CHECK(clock.now_us() == 2'750U);
    CHECK(machine.state() == Sht3xState::WaitAfterReset);
    CHECK(machine.next_deadline_us() == 4'750U);
}

void test_explicit_step_time_guards_against_regressed_clock() {
    FakeClock clock;
    FakeBus bus(clock);
    FakePublisher publisher;
    Sht3xStateMachine machine(bus, clock, publisher);

    CHECK(machine.step_at(2'000U).state_advanced);
    CHECK(machine.step_at(2'000U).bus_operation);
    CHECK(clock.now_us() == 0U);
    CHECK(machine.state() == Sht3xState::WaitAfterReset);
    CHECK(machine.next_deadline_us() == 4'000U);
}

void test_pressure_cadence_is_phase_anchored() {
    FakeClock clock;
    FakeBus bus(clock);
    FakePublisher publisher;
    bus.operation_duration_us = 100U;
    Ms5611StateMachine machine(bus, clock, publisher);
    std::array<std::uint64_t, 4> d1_start_times{};
    std::size_t d1_count = 0U;

    for (std::size_t step = 0U; step < 256U && d1_count < d1_start_times.size();
         ++step) {
        if (!machine.ready_at(clock.now_us())) {
            clock.set(machine.next_deadline_us());
        }
        const auto before = bus.operation_count;
        (void)machine.step();
        for (std::size_t index = before; index < bus.operation_count; ++index) {
            const auto& operation = bus.operation_at(index);
            if (!operation.recovery && operation.address == 0x77U &&
                operation.command == 0x0048U) {
                d1_start_times[d1_count++] = operation.started_at_us;
            }
        }
    }

    CHECK(d1_count == d1_start_times.size());
    if (d1_count == d1_start_times.size()) {
        CHECK(d1_start_times[1] - d1_start_times[0] <= 41'000U);
        CHECK(d1_start_times[2] - d1_start_times[1] >= 39'000U);
        CHECK(d1_start_times[2] - d1_start_times[1] <= 41'000U);
        CHECK(d1_start_times[3] - d1_start_times[2] >= 39'000U);
        CHECK(d1_start_times[3] - d1_start_times[2] <= 41'000U);
    }
}

void test_invalid_identity_configuration_fails_closed() {
    FakeClock clock;
    FakeBus bus(clock);
    FakePublisher publisher;
    Sht3xConfig sht_config{};
    sht_config.address = 0x46U;
    sht_config.repeatability =
        static_cast<shahbaz::sensors::sht3x::Repeatability>(0xFFU);
    sht_config.sample_interval_us = 0U;
    sht_config.conversion_margin_us = 0U;
    Ms5611Config ms_config{};
    ms_config.address = 0x75U;
    ms_config.pressure_osr =
        static_cast<shahbaz::sensors::ms5611::Osr>(0U);
    ms_config.temperature_osr =
        static_cast<shahbaz::sensors::ms5611::Osr>(0U);
    ms_config.pressure_interval_us = 0U;
    ms_config.temperature_interval_us = 0U;
    ms_config.conversion_margin_us = 0U;

    Sht3xStateMachine sht(bus, clock, publisher, sht_config);
    Ms5611StateMachine ms(bus, clock, publisher, ms_config);
    CHECK(!sht.health().configuration_valid);
    CHECK(!ms.health().configuration_valid);
    CHECK(sht.health().last_error == DriverError::InvalidConfiguration);
    CHECK(ms.health().last_error == DriverError::InvalidConfiguration);
    CHECK(sht.state() == Sht3xState::Offline);
    CHECK(ms.state() == Ms5611State::Offline);
    CHECK(sht.next_deadline_us() ==
          std::numeric_limits<std::uint64_t>::max());
    CHECK(ms.next_deadline_us() ==
          std::numeric_limits<std::uint64_t>::max());
    CHECK(!sht.step().state_advanced);
    CHECK(!ms.step().state_advanced);
    CHECK(bus.operation_count == 0U);
}

void test_sht_failure_does_not_stop_ms5611() {
    FakeClock clock;
    FakeBus bus(clock);
    FakePublisher publisher;
    bus.fail_sht_always = true;
    Sht3xConfig sht_config{};
    sht_config.retry.initial_backoff_us = 2'000U;
    sht_config.retry.maximum_backoff_us = 4'000U;
    sht_config.retry.offline_retry_us = 5'000'000U;
    sht_config.retry.maximum_consecutive_failures = 2U;
    SharedSensorScheduler scheduler(bus, clock, publisher, sht_config, {});

    drive(scheduler, clock, bus, 1'000U);

    CHECK(publisher.count_for(SensorId::Sht3x) == 0U);
    CHECK(publisher.count_for(SensorId::Ms5611) > 0U);
    CHECK(scheduler.sht3x().state() == Sht3xState::Offline);
    CHECK(scheduler.sht3x().health().total_failures == 2U);
    CHECK(scheduler.sht3x().health().recoveries == 1U);
    CHECK(scheduler.ms5611().health().online);
}

void test_ms5611_failure_does_not_stop_sht() {
    FakeClock clock;
    FakeBus bus(clock);
    FakePublisher publisher;
    bus.fail_next_ms_transfers = 1'000U;
    Ms5611Config ms_config{};
    ms_config.retry.initial_backoff_us = 2'000U;
    ms_config.retry.maximum_backoff_us = 4'000U;
    ms_config.retry.offline_retry_us = 5'000'000U;
    ms_config.retry.maximum_consecutive_failures = 2U;
    SharedSensorScheduler scheduler(bus, clock, publisher, {}, ms_config);

    drive(scheduler, clock, bus, 1'000U);

    CHECK(publisher.count_for(SensorId::Ms5611) == 0U);
    CHECK(publisher.count_for(SensorId::Sht3x) > 0U);
    CHECK(scheduler.ms5611().state() == Ms5611State::Offline);
    CHECK(scheduler.ms5611().health().total_failures == 2U);
    CHECK(scheduler.ms5611().health().recoveries == 1U);
    CHECK(scheduler.sht3x().health().online);
}

void test_crc_failures_retry_without_unnecessary_bus_recovery() {
    FakeClock clock;
    FakeBus bus(clock);
    FakePublisher publisher;
    bus.corrupt_sht_measurements = 1U;
    bus.corrupt_ms_prom_cycles = 1U;
    Sht3xConfig sht_config{};
    sht_config.retry.initial_backoff_us = 2'000U;
    Ms5611Config ms_config{};
    ms_config.retry.initial_backoff_us = 2'000U;
    SharedSensorScheduler scheduler(bus, clock, publisher, sht_config,
                                    ms_config);

    drive(scheduler, clock, bus, 1'200U);

    CHECK(scheduler.sht3x().health().crc_failures == 1U);
    CHECK(scheduler.ms5611().health().crc_failures == 1U);
    CHECK(bus.recovery_count == 0U);
    CHECK(publisher.count_for(SensorId::Sht3x) > 0U);
    CHECK(publisher.count_for(SensorId::Ms5611) > 0U);
    const auto* sht_sample = publisher.first_for(SensorId::Sht3x);
    const auto* ms_sample = publisher.first_for(SensorId::Ms5611);
    CHECK(sht_sample != nullptr &&
          has_quality(*sht_sample, QualityFlag::RecoveredAfterError));
    CHECK(ms_sample != nullptr &&
          has_quality(*ms_sample, QualityFlag::RecoveredAfterError));
}

void test_recovery_failure_is_finite_and_enters_offline() {
    FakeClock clock;
    FakeBus bus(clock);
    FakePublisher publisher;
    bus.fail_sht_always = true;
    bus.recovery_status = I2cStatus::RecoveryFailed;
    Sht3xConfig config{};
    config.retry.initial_backoff_us = 1'000U;
    config.retry.maximum_backoff_us = 1'000U;
    config.retry.offline_retry_us = 5'000'000U;
    config.retry.maximum_consecutive_failures = 2U;
    Sht3xStateMachine machine(bus, clock, publisher, config);

    for (std::size_t index = 0U; index < 64U &&
                                machine.state() != Sht3xState::Offline;
         ++index) {
        if (!machine.ready_at(clock.now_us())) {
            clock.set(machine.next_deadline_us());
        }
        const auto before = bus.operation_count;
        const auto result = machine.step();
        CHECK(bus.operation_count - before <= 1U);
        CHECK(!result.bus_operation || bus.operation_count - before == 1U);
    }
    CHECK(machine.state() == Sht3xState::Offline);
    CHECK(machine.health().recovery_failures == 1U);
    CHECK(bus.recovery_count == 1U);
}

void test_repeated_crc_failure_uses_one_bounded_recovery() {
    FakeClock clock;
    FakeBus bus(clock);
    FakePublisher publisher;
    bus.corrupt_sht_measurements = 2U;
    Sht3xConfig config{};
    config.retry.initial_backoff_us = 1'000U;
    config.retry.maximum_backoff_us = 2'000U;
    config.retry.maximum_consecutive_failures = 3U;
    SharedSensorScheduler scheduler(bus, clock, publisher, config, {});

    drive(scheduler, clock, bus, 1'000U);

    CHECK(scheduler.sht3x().health().crc_failures == 2U);
    CHECK(scheduler.sht3x().health().recoveries == 1U);
    CHECK(publisher.count_for(SensorId::Sht3x) > 0U);
}

void test_recovery_settle_is_a_shared_bus_barrier() {
    FakeClock clock;
    FakeBus bus(clock);
    FakePublisher publisher;
    bus.fail_next_sht_transfers = 1U;
    bus.operation_duration_us = 100U;
    Sht3xConfig config{};
    config.retry.initial_backoff_us = 1'000U;
    SharedSensorScheduler scheduler(bus, clock, publisher, config, {});
    std::uint64_t barrier = 0U;

    for (std::size_t index = 0U; index < 128U && barrier == 0U; ++index) {
        const auto result = scheduler.step();
        barrier = result.step.bus_settle_until_us;
        if (barrier == 0U) {
            clock.advance(500U);
        }
    }
    CHECK(barrier != 0U);
    if (barrier != 0U) {
        CHECK(scheduler.ms5611().health().shared_bus_invalidations == 1U);
        CHECK(scheduler.ms5611().state() == Ms5611State::RecoverySettle);
        const auto operations_after_recovery = bus.operation_count;
        CHECK(clock.now_us() < barrier);
        const auto blocked = scheduler.step();
        CHECK(blocked.sensor ==
              shahbaz::sensors::scheduler::ScheduledSensor::None);
        CHECK(bus.operation_count == operations_after_recovery);
        CHECK(scheduler.next_deadline_us() == barrier);
        clock.set(barrier - 1U);
        (void)scheduler.step();
        CHECK(bus.operation_count == operations_after_recovery);
        clock.set(barrier);
        const auto allowed = scheduler.step();
        CHECK(allowed.sensor !=
              shahbaz::sensors::scheduler::ScheduledSensor::None);
        drive(scheduler, clock, bus, 500U);
        CHECK(publisher.count_for(SensorId::Sht3x) > 0U);
        CHECK(publisher.count_for(SensorId::Ms5611) > 0U);
    }
}

void test_publication_backpressure_does_not_recover_bus() {
    FakeClock clock;
    FakeBus bus(clock);
    FakePublisher publisher;
    publisher.reject_next = 1U;
    Sht3xConfig config{};
    config.sample_interval_us = 20'000U;
    Sht3xStateMachine machine(bus, clock, publisher, config);

    for (std::size_t index = 0U; index < 200U &&
                                publisher.sample_count == 0U;
         ++index) {
        if (!machine.ready_at(clock.now_us())) {
            clock.set(machine.next_deadline_us());
        }
        (void)machine.step();
    }
    CHECK(machine.health().publication_drops == 1U);
    CHECK(machine.health().online);
    CHECK(machine.health().recoveries == 0U);
    CHECK(publisher.sample_count == 1U);
    CHECK(publisher.samples[0].sequence == 1U);
}

void test_unsafe_timing_configuration_is_clamped_and_reported() {
    FakeClock clock;
    FakeBus bus(clock);
    FakePublisher publisher;
    Sht3xConfig sht_config{};
    sht_config.transaction_timeout_us = 0U;
    sht_config.power_up_wait_us = 0U;
    sht_config.soft_reset_wait_us = 0U;
    sht_config.sample_interval_us = 0U;
    sht_config.retry.maximum_backoff_us = 0U;
    sht_config.retry.recovery_timeout_us = 0U;
    sht_config.retry.maximum_consecutive_failures = 0U;
    Ms5611Config ms_config{};
    ms_config.transaction_timeout_us = 0U;
    ms_config.reset_wait_us = 0U;
    ms_config.pressure_interval_us = 0U;
    ms_config.temperature_interval_us = 0U;
    ms_config.maximum_temperature_age_us = 0U;
    ms_config.retry.maximum_backoff_us = 0U;
    ms_config.retry.recovery_timeout_us = 0U;
    ms_config.retry.maximum_consecutive_failures = 0U;
    SharedSensorScheduler scheduler(bus, clock, publisher, sht_config,
                                    ms_config);

    CHECK(scheduler.sht3x().health().configuration_clamped);
    CHECK(scheduler.ms5611().health().configuration_clamped);
    CHECK(scheduler.sht3x().health().sampling_rate_clamped);
    CHECK(scheduler.ms5611().health().sampling_rate_clamped);
    drive(scheduler, clock, bus, 400U);
    const auto* sht_sample = publisher.first_for(SensorId::Sht3x);
    const auto* ms_sample = publisher.first_for(SensorId::Ms5611);
    CHECK(sht_sample != nullptr &&
          has_quality(*sht_sample, QualityFlag::RateLimited));
    CHECK(ms_sample != nullptr &&
          has_quality(*ms_sample, QualityFlag::RateLimited));
}

}  // namespace

int main() {
    test_nominal_shared_schedule_and_order();
    test_sht_deadlines_do_not_read_or_wait_early();
    test_sht_latched_status_is_retained_and_only_documented_flags_clear();
    test_sht_post_reset_self_test_is_always_high_repeatability();
    test_ms_adc_deadline_is_enforced();
    test_deadline_starts_after_bounded_transfer_completion();
    test_explicit_step_time_guards_against_regressed_clock();
    test_pressure_cadence_is_phase_anchored();
    test_invalid_identity_configuration_fails_closed();
    test_sht_failure_does_not_stop_ms5611();
    test_ms5611_failure_does_not_stop_sht();
    test_crc_failures_retry_without_unnecessary_bus_recovery();
    test_recovery_failure_is_finite_and_enters_offline();
    test_repeated_crc_failure_uses_one_bounded_recovery();
    test_recovery_settle_is_a_shared_bus_barrier();
    test_publication_backpressure_does_not_recover_bus();
    test_unsafe_timing_configuration_is_clamped_and_reported();

    if (failures != 0) {
        std::fprintf(stderr, "%d sensor scheduler test(s) failed\n", failures);
        return 1;
    }
    std::puts("All sensor scheduler tests passed");
    return 0;
}
