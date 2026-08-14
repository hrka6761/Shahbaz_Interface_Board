/**
 * @file ms5611_domain_test.cpp
 * @brief Deterministic host tests for the production MS5611 domain code.
 */
#include "sensor_ms5611/ms5611_domain.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>

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

using shahbaz::sensors::ms5611::Calibration;
using shahbaz::sensors::ms5611::ConversionKind;
using shahbaz::sensors::ms5611::CsbLevel;
using shahbaz::sensors::ms5611::Error;
using shahbaz::sensors::ms5611::Osr;
using shahbaz::sensors::ms5611::PromImage;

constexpr Calibration kDatasheetCalibration{
    40'127U, 36'924U, 23'317U, 23'282U, 33'464U, 28'312U,
};

void test_address_and_commands() {
    CHECK(shahbaz::sensors::ms5611::address_for_csb(CsbLevel::Low).address ==
          0x77U);
    CHECK(shahbaz::sensors::ms5611::address_for_csb(CsbLevel::High).address ==
          0x76U);
    CHECK(shahbaz::sensors::ms5611::address_for_csb(CsbLevel::Floating).error ==
          Error::InvalidCsbLevel);
    CHECK(shahbaz::sensors::ms5611::kResetCommand == 0x1EU);
    CHECK(shahbaz::sensors::ms5611::kAdcReadCommand == 0x00U);

    constexpr std::array<Osr, 5> osrs{
        Osr::Osr256, Osr::Osr512, Osr::Osr1024, Osr::Osr2048, Osr::Osr4096};
    constexpr std::array<std::uint8_t, 5> d1_commands{
        0x40U, 0x42U, 0x44U, 0x46U, 0x48U};
    constexpr std::array<std::uint8_t, 5> d2_commands{
        0x50U, 0x52U, 0x54U, 0x56U, 0x58U};
    constexpr std::array<std::uint32_t, 5> maximum_us{
        600U, 1'170U, 2'280U, 4'540U, 9'040U};

    for (std::size_t index = 0U; index < osrs.size(); ++index) {
        const auto d1 = shahbaz::sensors::ms5611::conversion_profile(
            ConversionKind::PressureD1, osrs[index]);
        const auto d2 = shahbaz::sensors::ms5611::conversion_profile(
            ConversionKind::TemperatureD2, osrs[index]);
        CHECK(d1.command == d1_commands[index]);
        CHECK(d2.command == d2_commands[index]);
        CHECK(d1.maximum_duration_us == maximum_us[index]);
        CHECK(d2.maximum_duration_us == maximum_us[index]);
        CHECK(d1.typical_duration_us < d1.maximum_duration_us);
    }

    for (std::uint8_t index = 0U; index < 8U; ++index) {
        CHECK(shahbaz::sensors::ms5611::prom_read_command(index) ==
              static_cast<std::uint8_t>(0xA0U + 2U * index));
    }
    CHECK(shahbaz::sensors::ms5611::prom_read_command(8U) == 0U);
}

void test_big_endian_decoding() {
    constexpr std::array<std::uint8_t, 2> word{0x9CU, 0xBFU};
    const auto decoded_word = shahbaz::sensors::ms5611::decode_prom_word(
        word.data(), word.size());
    CHECK(decoded_word.ok());
    CHECK(decoded_word.value == 40'127U);
    CHECK(shahbaz::sensors::ms5611::decode_prom_word(word.data(), 1U).error ==
          Error::InvalidLength);
    CHECK(shahbaz::sensors::ms5611::decode_prom_word(nullptr, 2U).error ==
          Error::NullData);

    constexpr std::array<std::uint8_t, 3> adc{0x8AU, 0xA2U, 0x1AU};
    const auto decoded_adc =
        shahbaz::sensors::ms5611::decode_adc(adc.data(), adc.size());
    CHECK(decoded_adc.ok());
    CHECK(decoded_adc.value == 9'085'466U);

    constexpr std::array<std::uint8_t, 3> zero_adc{0U, 0U, 0U};
    CHECK(shahbaz::sensors::ms5611::decode_adc(zero_adc.data(),
                                               zero_adc.size())
              .error == Error::ZeroAdcResult);
    CHECK(shahbaz::sensors::ms5611::decode_adc(adc.data(), 2U).error ==
          Error::InvalidLength);
    CHECK(shahbaz::sensors::ms5611::decode_adc(nullptr, 3U).error ==
          Error::NullData);
}

void test_prom_crc_and_calibration_validation() {
    // Fixed regression vector independently evaluated from the TE AN520 byte
    // order and 0x3000 remainder procedure. Word 7's stored CRC nibble is 5.
    const PromImage valid{
        0xA5A5U, 40'127U, 36'924U, 23'317U,
        23'282U, 33'464U, 28'312U, 0x0005U,
    };
    const auto original = valid;
    CHECK(shahbaz::sensors::ms5611::calculate_prom_crc4(valid) == 0x05U);
    CHECK(valid == original);

    const auto validation = shahbaz::sensors::ms5611::validate_prom(valid);
    CHECK(validation.ok());
    CHECK(validation.stored_crc == 0x05U);
    CHECK(validation.calculated_crc == 0x05U);
    CHECK(validation.calibration.pressure_sensitivity_c1 == 40'127U);
    CHECK(validation.calibration.temperature_coefficient_c6 == 28'312U);

    auto single_bit_corruption = valid;
    single_bit_corruption[1] ^= 0x0001U;
    CHECK(shahbaz::sensors::ms5611::calculate_prom_crc4(
              single_bit_corruption) == 0x01U);
    CHECK(shahbaz::sensors::ms5611::validate_prom(single_bit_corruption).error ==
          Error::PromCrcMismatch);

    auto invalid_stored_crc = valid;
    invalid_stored_crc[7] = 0x0004U;
    CHECK(shahbaz::sensors::ms5611::validate_prom(invalid_stored_crc).error ==
          Error::PromCrcMismatch);

    const PromImage all_zero{};
    CHECK(shahbaz::sensors::ms5611::validate_prom(all_zero).error ==
          Error::CalibrationAllZero);

    PromImage raw_all_one{};
    raw_all_one.fill(0xFFFFU);
    CHECK(shahbaz::sensors::ms5611::calculate_prom_crc4(raw_all_one) == 0U);
    CHECK(shahbaz::sensors::ms5611::validate_prom(raw_all_one).error ==
          Error::PromCrcMismatch);

    auto all_one_with_valid_crc = raw_all_one;
    all_one_with_valid_crc[7] = 0xFF00U;
    CHECK(shahbaz::sensors::ms5611::validate_prom(all_one_with_valid_crc).error ==
          Error::CalibrationAllOne);
}

void test_second_order_boundaries() {
    const auto above_twenty =
        shahbaz::sensors::ms5611::calculate_second_order(65'536, 2'001);
    const auto at_twenty =
        shahbaz::sensors::ms5611::calculate_second_order(65'536, 2'000);
    const auto below_twenty =
        shahbaz::sensors::ms5611::calculate_second_order(65'536, 1'999);
    CHECK(above_twenty.ok());
    CHECK(above_twenty.value.temperature2 == 0);
    CHECK(at_twenty.ok());
    CHECK(at_twenty.value.offset2 == 0);
    CHECK(below_twenty.ok());
    CHECK(below_twenty.value.temperature2 == 2);
    CHECK(below_twenty.value.offset2 == 2);
    CHECK(below_twenty.value.sensitivity2 == 1);

    const auto above_minus_fifteen =
        shahbaz::sensors::ms5611::calculate_second_order(0, -1'499);
    const auto at_minus_fifteen =
        shahbaz::sensors::ms5611::calculate_second_order(0, -1'500);
    const auto below_minus_fifteen =
        shahbaz::sensors::ms5611::calculate_second_order(0, -1'501);
    CHECK(above_minus_fifteen.ok());
    CHECK(at_minus_fifteen.ok());
    CHECK(below_minus_fifteen.ok());
    CHECK(at_minus_fifteen.value.offset2 == 30'625'000);
    CHECK(at_minus_fifteen.value.sensitivity2 == 15'312'500);
    CHECK(below_minus_fifteen.value.offset2 == 30'642'509);
    CHECK(below_minus_fifteen.value.sensitivity2 == 15'321'256);
    CHECK(below_minus_fifteen.value.offset2 >
          above_minus_fifteen.value.offset2);

    CHECK(shahbaz::sensors::ms5611::calculate_second_order(
              std::numeric_limits<std::int64_t>::max(), 1'999)
              .error == Error::ArithmeticOutOfRange);
    CHECK(shahbaz::sensors::ms5611::calculate_second_order(
              0, std::numeric_limits<std::int64_t>::min())
              .error == Error::ArithmeticOutOfRange);
}

void test_official_compensation_example() {
    const auto result = shahbaz::sensors::ms5611::compensate(
        kDatasheetCalibration, 9'085'466U, 8'569'150U);
    CHECK(result.ok());
    CHECK(result.first_order.temperature_delta == 2'366);
    CHECK(result.first_order.temperature_hundredths_celsius == 2'007);
    CHECK(result.first_order.offset == 2'420'281'617LL);
    CHECK(result.first_order.sensitivity == 1'315'097'036LL);
    CHECK(result.first_order.pressure_pascal == 100'009);
    CHECK(result.second_order.temperature2 == 0);
    CHECK(result.value.internal_temperature_milli_celsius == 20'070);
    CHECK(result.value.pressure_pascal == 100'009);
}

void test_compensation_errors_and_wide_intermediates() {
    CHECK(shahbaz::sensors::ms5611::compensate(kDatasheetCalibration, 0U,
                                               8'569'150U)
              .error == Error::ZeroAdcResult);
    CHECK(shahbaz::sensors::ms5611::compensate(kDatasheetCalibration,
                                               9'085'466U, 0U)
              .error == Error::ZeroAdcResult);
    CHECK(shahbaz::sensors::ms5611::compensate(
              kDatasheetCalibration, 0x0100'0000U, 8'569'150U)
              .error == Error::AdcValueOutOfRange);

    constexpr Calibration all_zero{};
    CHECK(shahbaz::sensors::ms5611::compensate(all_zero, 1U, 1U).error ==
          Error::CalibrationAllZero);

    const auto low_temperature = shahbaz::sensors::ms5611::compensate(
        kDatasheetCalibration, 9'085'466U, 7'000'000U);
    CHECK(low_temperature.ok());
    CHECK(low_temperature.first_order.temperature_delta < 0);
    CHECK(low_temperature.second_order.temperature2 > 0);
    CHECK(low_temperature.value.internal_temperature_milli_celsius <
          low_temperature.first_order.temperature_hundredths_celsius * 10);

    constexpr Calibration large_coefficients{
        0xFFFEU, 0xFFFEU, 0xFFFEU, 0xFFFEU, 0xFFFEU, 0xFFFEU};
    const auto extreme = shahbaz::sensors::ms5611::compensate(
        large_coefficients, 0x00FF'FFFFU, 0x00FF'FFFFU);
    CHECK(extreme.ok());
    CHECK(extreme.value.internal_temperature_milli_celsius == 20'030);
    CHECK(extreme.value.pressure_pascal == 393'227);
}

}  // namespace

int main() {
    test_address_and_commands();
    test_big_endian_decoding();
    test_prom_crc_and_calibration_validation();
    test_second_order_boundaries();
    test_official_compensation_example();
    test_compensation_errors_and_wide_intermediates();

    if (failures != 0) {
        std::cerr << failures << " MS5611 domain test(s) failed\n";
        return 1;
    }
    std::cout << "All MS5611 domain tests passed\n";
    return 0;
}
