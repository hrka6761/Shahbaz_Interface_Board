/**
 * @file sht3x_domain_test.cpp
 * @brief Deterministic host tests for the production SHT3x domain code.
 */
#include "sensor_sht30/sht3x_domain.hpp"

#include <array>
#include <cstdint>
#include <iostream>

namespace {

int failures = 0;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                      \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                 \
            ++failures;                                                         \
        }                                                                       \
    } while (false)

using shahbaz::sensors::sht3x::Error;
using shahbaz::sensors::sht3x::Repeatability;

void test_crc_vectors() {
    constexpr std::array<std::uint8_t, 2> official{0xBEU, 0xEFU};
    CHECK(shahbaz::sensors::sht3x::crc8(official.data(), official.size()) ==
          0x92U);

    constexpr std::array<std::uint8_t, 2> zeros{0x00U, 0x00U};
    CHECK(shahbaz::sensors::sht3x::crc8(zeros.data(), zeros.size()) == 0x81U);
}

void test_profiles_and_timing() {
    const auto high =
        shahbaz::sensors::sht3x::measurement_profile(Repeatability::High);
    const auto medium =
        shahbaz::sensors::sht3x::measurement_profile(Repeatability::Medium);
    const auto low =
        shahbaz::sensors::sht3x::measurement_profile(Repeatability::Low);

    CHECK(high.command == 0x2400U);
    CHECK(high.typical_duration_us == 12'500U);
    CHECK(high.maximum_duration_us == 15'500U);
    CHECK(medium.command == 0x240BU);
    CHECK(medium.maximum_duration_us == 6'500U);
    CHECK(low.command == 0x2416U);
    CHECK(low.maximum_duration_us == 4'500U);
    CHECK(shahbaz::sensors::sht3x::kMinimumCommandSpacingUs == 1'000U);
    CHECK(shahbaz::sensors::sht3x::kDefaultHighRepeatabilityDeadlineUs >
          high.maximum_duration_us);
}

void test_integer_conversion_endpoints_and_rounding() {
    using shahbaz::sensors::sht3x::relative_humidity_milli_percent;
    using shahbaz::sensors::sht3x::temperature_milli_celsius;

    CHECK(temperature_milli_celsius(0U) == -45'000);
    CHECK(temperature_milli_celsius(65'535U) == 130'000);
    CHECK(relative_humidity_milli_percent(0U) == 0U);
    CHECK(relative_humidity_milli_percent(65'535U) == 100'000U);
    CHECK(temperature_milli_celsius(32'768U) == 42'501);
    CHECK(relative_humidity_milli_percent(32'768U) == 50'001U);

    auto prior_temperature = temperature_milli_celsius(0U);
    auto prior_humidity = relative_humidity_milli_percent(0U);
    for (std::uint32_t raw = 1U; raw <= 65'535U; ++raw) {
        const auto converted_temperature =
            temperature_milli_celsius(static_cast<std::uint16_t>(raw));
        const auto converted_humidity = relative_humidity_milli_percent(
            static_cast<std::uint16_t>(raw));
        CHECK(converted_temperature >= prior_temperature);
        CHECK(converted_humidity >= prior_humidity);
        prior_temperature = converted_temperature;
        prior_humidity = converted_humidity;
    }
}

void test_measurement_parsing_and_atomic_crc_rejection() {
    constexpr std::array<std::uint8_t, 6> valid{
        0xBEU, 0xEFU, 0x92U, 0x00U, 0x00U, 0x81U};
    const auto parsed = shahbaz::sensors::sht3x::parse_measurement(
        valid.data(), valid.size());
    CHECK(parsed.ok());
    CHECK(parsed.value.raw_temperature == 0xBEEFU);
    CHECK(parsed.value.raw_relative_humidity == 0U);
    CHECK(parsed.value.ambient_temperature_milli_celsius == 85'523);
    CHECK(parsed.value.relative_humidity_milli_percent == 0U);
    CHECK(!parsed.value.temperature_outside_specified_range);

    auto bad_temperature_crc = valid;
    bad_temperature_crc[2] ^= 0x01U;
    CHECK(shahbaz::sensors::sht3x::parse_measurement(
              bad_temperature_crc.data(), bad_temperature_crc.size())
              .error == Error::TemperatureCrcMismatch);

    auto bad_humidity_crc = valid;
    bad_humidity_crc[5] ^= 0x01U;
    CHECK(shahbaz::sensors::sht3x::parse_measurement(
              bad_humidity_crc.data(), bad_humidity_crc.size())
              .error == Error::HumidityCrcMismatch);

    CHECK(shahbaz::sensors::sht3x::parse_measurement(nullptr, valid.size())
              .error == Error::NullData);
    CHECK(shahbaz::sensors::sht3x::parse_measurement(valid.data(), 5U).error ==
          Error::InvalidLength);
}

void test_status_parsing() {
    // 0xAC13 selects every status flag represented by the domain type.
    constexpr std::array<std::uint8_t, 3> status_bytes{0xACU, 0x13U, 0xDAU};
    const auto parsed = shahbaz::sensors::sht3x::parse_status(
        status_bytes.data(), status_bytes.size());
    CHECK(parsed.ok());
    CHECK(parsed.value.raw_word == 0xAC13U);
    CHECK(parsed.value.alert_pending);
    CHECK(parsed.value.heater_enabled);
    CHECK(parsed.value.relative_humidity_tracking_alert);
    CHECK(parsed.value.temperature_tracking_alert);
    CHECK(parsed.value.system_reset_detected);
    CHECK(parsed.value.last_command_failed);
    CHECK(parsed.value.last_write_checksum_failed);

    auto bad_crc = status_bytes;
    bad_crc[2] ^= 0x01U;
    CHECK(shahbaz::sensors::sht3x::parse_status(bad_crc.data(), bad_crc.size())
              .error == Error::StatusCrcMismatch);
    CHECK(shahbaz::sensors::sht3x::parse_status(status_bytes.data(), 2U).error ==
          Error::InvalidLength);
}

void test_formula_range_is_not_silently_clamped() {
    constexpr std::array<std::uint8_t, 6> endpoint{
        0xFFU, 0xFFU, 0xACU, 0xFFU, 0xFFU, 0xACU};
    const auto parsed = shahbaz::sensors::sht3x::parse_measurement(
        endpoint.data(), endpoint.size());
    CHECK(parsed.ok());
    CHECK(parsed.value.ambient_temperature_milli_celsius == 130'000);
    CHECK(parsed.value.relative_humidity_milli_percent == 100'000U);
    CHECK(parsed.value.temperature_outside_specified_range);
}

}  // namespace

int main() {
    test_crc_vectors();
    test_profiles_and_timing();
    test_integer_conversion_endpoints_and_rounding();
    test_measurement_parsing_and_atomic_crc_rejection();
    test_status_parsing();
    test_formula_range_is_not_silently_clamped();

    if (failures != 0) {
        std::cerr << failures << " SHT3x domain test(s) failed\n";
        return 1;
    }
    std::cout << "All SHT3x domain tests passed\n";
    return 0;
}
