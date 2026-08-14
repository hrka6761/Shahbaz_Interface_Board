/**
 * @file ms5611_domain.cpp
 * @brief Allocation-free implementation of PROM CRC and compensation logic.
 */
#include "sensor_ms5611/ms5611_domain.hpp"

#include <limits>

namespace shahbaz::sensors::ms5611 {
namespace {

constexpr std::int64_t divide_truncating_toward_zero(
    const std::int64_t numerator, const std::int64_t denominator) noexcept {
    return numerator / denominator;
}

bool checked_square(const std::int64_t value,
                    const std::int64_t multiplier,
                    std::int64_t& square) noexcept {
    if (value == std::numeric_limits<std::int64_t>::min() ||
        multiplier <= 0) {
        return false;
    }
    const std::int64_t magnitude = value < 0 ? -value : value;
    const std::int64_t maximum_before_multiply =
        std::numeric_limits<std::int64_t>::max() / multiplier;
    if (magnitude != 0 &&
        magnitude > maximum_before_multiply / magnitude) {
        return false;
    }
    square = magnitude * magnitude;
    return true;
}

bool calibration_is_all(const Calibration& calibration,
                        const std::uint16_t value) noexcept {
    return calibration.pressure_sensitivity_c1 == value &&
           calibration.pressure_offset_c2 == value &&
           calibration.sensitivity_temperature_coefficient_c3 == value &&
           calibration.offset_temperature_coefficient_c4 == value &&
           calibration.reference_temperature_c5 == value &&
           calibration.temperature_coefficient_c6 == value;
}

Error validate_calibration(const Calibration& calibration) noexcept {
    if (calibration_is_all(calibration, 0U)) {
        return Error::CalibrationAllZero;
    }
    if (calibration_is_all(calibration, 0xFFFFU)) {
        return Error::CalibrationAllOne;
    }
    return Error::None;
}

}  // namespace

WordResult decode_prom_word(const std::uint8_t* const data,
                            const std::size_t size) noexcept {
    if (data == nullptr) {
        return {Error::NullData, 0U};
    }
    if (size != 2U) {
        return {Error::InvalidLength, 0U};
    }
    return {Error::None,
            static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(data[0]) << 8U) |
                static_cast<std::uint16_t>(data[1]))};
}

AdcResult decode_adc(const std::uint8_t* const data,
                     const std::size_t size) noexcept {
    if (data == nullptr) {
        return {Error::NullData, 0U};
    }
    if (size != 3U) {
        return {Error::InvalidLength, 0U};
    }
    const std::uint32_t value =
        (static_cast<std::uint32_t>(data[0]) << 16U) |
        (static_cast<std::uint32_t>(data[1]) << 8U) |
        static_cast<std::uint32_t>(data[2]);
    if (value == 0U) {
        return {Error::ZeroAdcResult, 0U};
    }
    return {Error::None, value};
}

std::uint8_t calculate_prom_crc4(const PromImage& prom) noexcept {
    PromImage scratch = prom;
    scratch[7] = static_cast<std::uint16_t>(scratch[7] & 0xFF00U);

    std::uint16_t remainder = 0U;
    for (std::uint8_t byte_index = 0U; byte_index < 16U; ++byte_index) {
        const std::uint16_t word = scratch[byte_index >> 1U];
        const std::uint16_t next_byte =
            (byte_index & 1U) != 0U
                ? static_cast<std::uint16_t>(word & 0x00FFU)
                : static_cast<std::uint16_t>(word >> 8U);
        remainder = static_cast<std::uint16_t>(remainder ^ next_byte);

        for (std::uint8_t bit = 0U; bit < 8U; ++bit) {
            remainder = (remainder & 0x8000U) != 0U
                            ? static_cast<std::uint16_t>(
                                  (remainder << 1U) ^ 0x3000U)
                            : static_cast<std::uint16_t>(remainder << 1U);
        }
    }
    return static_cast<std::uint8_t>((remainder >> 12U) & 0x000FU);
}

PromValidationResult validate_prom(const PromImage& prom) noexcept {
    const auto stored_crc = static_cast<std::uint8_t>(prom[7] & 0x000FU);
    const auto calculated_crc = calculate_prom_crc4(prom);
    const Calibration calibration{
        prom[1], prom[2], prom[3], prom[4], prom[5], prom[6],
    };

    if (stored_crc != calculated_crc) {
        return {Error::PromCrcMismatch, stored_crc, calculated_crc, {}};
    }
    const auto calibration_error = validate_calibration(calibration);
    if (calibration_error != Error::None) {
        return {calibration_error, stored_crc, calculated_crc, {}};
    }
    return {Error::None, stored_crc, calculated_crc, calibration};
}

SecondOrderResult calculate_second_order(
    const std::int64_t temperature_delta,
    const std::int64_t first_order_temperature_hundredths_celsius) noexcept {
    if (first_order_temperature_hundredths_celsius >= 2'000) {
        return {Error::None, {0, 0, 0}};
    }
    if (first_order_temperature_hundredths_celsius <
        std::numeric_limits<std::int64_t>::min() + 2'000) {
        return {Error::ArithmeticOutOfRange, {}};
    }

    std::int64_t delta_temperature_square = 0;
    std::int64_t below_twenty_square = 0;
    if (!checked_square(temperature_delta, 1, delta_temperature_square) ||
        !checked_square(first_order_temperature_hundredths_celsius - 2'000,
                        5, below_twenty_square)) {
        return {Error::ArithmeticOutOfRange, {}};
    }

    SecondOrderCorrection correction{
        divide_truncating_toward_zero(delta_temperature_square, 2'147'483'648LL),
        divide_truncating_toward_zero(5 * below_twenty_square, 2),
        divide_truncating_toward_zero(5 * below_twenty_square, 4),
    };

    if (first_order_temperature_hundredths_celsius < -1'500) {
        std::int64_t below_minus_fifteen_square = 0;
        if (!checked_square(
                first_order_temperature_hundredths_celsius + 1'500, 11,
                below_minus_fifteen_square)) {
            return {Error::ArithmeticOutOfRange, {}};
        }
        const auto additional_offset = 7 * below_minus_fifteen_square;
        const auto additional_sensitivity = divide_truncating_toward_zero(
            11 * below_minus_fifteen_square, 2);
        if (correction.offset2 >
                std::numeric_limits<std::int64_t>::max() - additional_offset ||
            correction.sensitivity2 >
                std::numeric_limits<std::int64_t>::max() -
                    additional_sensitivity) {
            return {Error::ArithmeticOutOfRange, {}};
        }
        correction.offset2 += additional_offset;
        correction.sensitivity2 += additional_sensitivity;
    }

    return {Error::None, correction};
}

CompensationResult compensate(const Calibration& calibration,
                              const std::uint32_t raw_pressure_d1,
                              const std::uint32_t raw_temperature_d2) noexcept {
    const auto calibration_error = validate_calibration(calibration);
    if (calibration_error != Error::None) {
        return {calibration_error, {}, {}, {}};
    }
    if (raw_pressure_d1 == 0U || raw_temperature_d2 == 0U) {
        return {Error::ZeroAdcResult, {}, {}, {}};
    }
    if (raw_pressure_d1 > kMaximumAdcValue ||
        raw_temperature_d2 > kMaximumAdcValue) {
        return {Error::AdcValueOutOfRange, {}, {}, {}};
    }

    const auto d1 = static_cast<std::int64_t>(raw_pressure_d1);
    const auto d2 = static_cast<std::int64_t>(raw_temperature_d2);
    const auto c1 = static_cast<std::int64_t>(
        calibration.pressure_sensitivity_c1);
    const auto c2 =
        static_cast<std::int64_t>(calibration.pressure_offset_c2);
    const auto c3 = static_cast<std::int64_t>(
        calibration.sensitivity_temperature_coefficient_c3);
    const auto c4 = static_cast<std::int64_t>(
        calibration.offset_temperature_coefficient_c4);
    const auto c5 = static_cast<std::int64_t>(
        calibration.reference_temperature_c5);
    const auto c6 = static_cast<std::int64_t>(
        calibration.temperature_coefficient_c6);

    const std::int64_t temperature_delta = d2 - c5 * 256;
    const std::int64_t first_temperature =
        2'000 + divide_truncating_toward_zero(temperature_delta * c6,
                                              8'388'608);
    const std::int64_t first_offset =
        c2 * 65'536 +
        divide_truncating_toward_zero(c4 * temperature_delta, 128);
    const std::int64_t first_sensitivity =
        c1 * 32'768 +
        divide_truncating_toward_zero(c3 * temperature_delta, 256);
    const std::int64_t first_pressure = divide_truncating_toward_zero(
        divide_truncating_toward_zero(d1 * first_sensitivity, 2'097'152) -
            first_offset,
        32'768);

    const FirstOrderValues first_order{
        temperature_delta,
        first_temperature,
        first_offset,
        first_sensitivity,
        first_pressure,
    };

    const auto correction =
        calculate_second_order(temperature_delta, first_temperature);
    if (!correction.ok()) {
        return {correction.error, {}, first_order, {}};
    }

    const std::int64_t corrected_temperature =
        first_temperature - correction.value.temperature2;
    const std::int64_t corrected_offset =
        first_offset - correction.value.offset2;
    const std::int64_t corrected_sensitivity =
        first_sensitivity - correction.value.sensitivity2;
    const std::int64_t corrected_pressure = divide_truncating_toward_zero(
        divide_truncating_toward_zero(d1 * corrected_sensitivity, 2'097'152) -
            corrected_offset,
        32'768);

    constexpr auto kInt32Min =
        static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min());
    constexpr auto kInt32Max =
        static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max());
    if (corrected_temperature < kInt32Min / 10 ||
        corrected_temperature > kInt32Max / 10 ||
        corrected_pressure < kInt32Min || corrected_pressure > kInt32Max) {
        return {Error::ArithmeticOutOfRange, {}, first_order, correction.value};
    }

    const CompensatedSample sample{
        raw_pressure_d1,
        raw_temperature_d2,
        static_cast<std::int32_t>(corrected_temperature * 10),
        static_cast<std::int32_t>(corrected_pressure),
    };
    return {Error::None, sample, first_order, correction.value};
}

}  // namespace shahbaz::sensors::ms5611
