/**
 * @file ms5611_domain.hpp
 * @brief Pure C++17 MS5611 commands, PROM validation, and compensation math.
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace shahbaz::sensors::ms5611 {

/** @name I2C addresses, commands, and hardware-independent timing limits. */
///@{
inline constexpr std::uint8_t kAddressCsbHigh = 0x76U;
inline constexpr std::uint8_t kAddressCsbLow = 0x77U;
inline constexpr std::uint8_t kResetCommand = 0x1EU;
inline constexpr std::uint8_t kAdcReadCommand = 0x00U;
inline constexpr std::uint8_t kPromReadBaseCommand = 0xA0U;
inline constexpr std::uint32_t kMinimumResetWaitUs = 3'000U;
inline constexpr std::uint32_t kDefaultResetWaitUs = 4'000U;
inline constexpr std::uint32_t kMaximumAdcValue = 0x00FF'FFFFU;
///@}

/** Complete eight-word PROM image in command order 0xA0 through 0xAE. */
using PromImage = std::array<std::uint16_t, 8>;

/** CSB level used to derive the seven-bit I2C address. */
enum class CsbLevel : std::uint8_t {
    Low,
    High,
    Floating,
};

/** Selects the shared ADC's pressure (D1) or temperature (D2) conversion. */
enum class ConversionKind : std::uint8_t {
    PressureD1,
    TemperatureD2,
};

/** Supported oversampling-ratio values. */
enum class Osr : std::uint16_t {
    Osr256 = 256,
    Osr512 = 512,
    Osr1024 = 1024,
    Osr2048 = 2048,
    Osr4096 = 4096,
};

/** One-byte command and datasheet timing for a conversion. */
struct ConversionProfile {
    std::uint8_t command;
    std::uint32_t typical_duration_us;
    std::uint32_t maximum_duration_us;
};

/** Returns the typed D1/D2 command and timing for an OSR. */
[[nodiscard]] constexpr ConversionProfile conversion_profile(
    const ConversionKind kind, const Osr osr) noexcept {
    std::uint8_t osr_bits = 0U;
    std::uint32_t typical_us = 0U;
    std::uint32_t maximum_us = 0U;
    switch (osr) {
        case Osr::Osr256:
            osr_bits = 0x00U;
            typical_us = 480U;
            maximum_us = 600U;
            break;
        case Osr::Osr512:
            osr_bits = 0x02U;
            typical_us = 950U;
            maximum_us = 1'170U;
            break;
        case Osr::Osr1024:
            osr_bits = 0x04U;
            typical_us = 1'880U;
            maximum_us = 2'280U;
            break;
        case Osr::Osr2048:
            osr_bits = 0x06U;
            typical_us = 3'720U;
            maximum_us = 4'540U;
            break;
        case Osr::Osr4096:
            osr_bits = 0x08U;
            typical_us = 7'400U;
            maximum_us = 9'040U;
            break;
    }

    const std::uint8_t kind_bits =
        kind == ConversionKind::TemperatureD2 ? 0x10U : 0x00U;
    return {static_cast<std::uint8_t>(0x40U | kind_bits | osr_bits),
            typical_us, maximum_us};
}

/**
 * Returns the PROM read command for index 0..7, or zero for an invalid index.
 */
[[nodiscard]] constexpr std::uint8_t prom_read_command(
    const std::uint8_t index) noexcept {
    return index < 8U
               ? static_cast<std::uint8_t>(kPromReadBaseCommand +
                                           static_cast<std::uint8_t>(2U * index))
               : 0U;
}

/** Domain validation failures; bus/timeout failures belong to the adapter. */
enum class Error : std::uint8_t {
    None,
    InvalidCsbLevel,
    NullData,
    InvalidLength,
    ZeroAdcResult,
    AdcValueOutOfRange,
    PromCrcMismatch,
    CalibrationAllZero,
    CalibrationAllOne,
    ArithmeticOutOfRange,
};

/** Typed result for CSB-to-address selection. */
struct AddressResult {
    Error error{Error::None};
    std::uint8_t address{0U};

    [[nodiscard]] constexpr bool ok() const noexcept {
        return error == Error::None;
    }
};

/** Typed result for a decoded 16-bit PROM word. */
struct WordResult {
    Error error{Error::None};
    std::uint16_t value{0U};

    [[nodiscard]] constexpr bool ok() const noexcept {
        return error == Error::None;
    }
};

/** Typed result for a decoded, nonzero 24-bit ADC value. */
struct AdcResult {
    Error error{Error::None};
    std::uint32_t value{0U};

    [[nodiscard]] constexpr bool ok() const noexcept {
        return error == Error::None;
    }
};

/** Factory coefficients C1 through C6 with their MS5611 meanings. */
struct Calibration {
    std::uint16_t pressure_sensitivity_c1;
    std::uint16_t pressure_offset_c2;
    std::uint16_t sensitivity_temperature_coefficient_c3;
    std::uint16_t offset_temperature_coefficient_c4;
    std::uint16_t reference_temperature_c5;
    std::uint16_t temperature_coefficient_c6;
};

/** CRC result plus usable calibration when validation succeeds. */
struct PromValidationResult {
    Error error{Error::None};
    std::uint8_t stored_crc{0U};
    std::uint8_t calculated_crc{0U};
    Calibration calibration{};

    [[nodiscard]] constexpr bool ok() const noexcept {
        return error == Error::None;
    }
};

/** First-order datasheet intermediates retained for diagnostics and tests. */
struct FirstOrderValues {
    std::int64_t temperature_delta;
    std::int64_t temperature_hundredths_celsius;
    std::int64_t offset;
    std::int64_t sensitivity;
    std::int64_t pressure_pascal;
};

/** Low-temperature corrections T2, OFF2, and SENS2. */
struct SecondOrderCorrection {
    std::int64_t temperature2;
    std::int64_t offset2;
    std::int64_t sensitivity2;
};

/** Typed result for second-order calculation and overflow checks. */
struct SecondOrderResult {
    Error error{Error::None};
    SecondOrderCorrection value{};

    [[nodiscard]] constexpr bool ok() const noexcept {
        return error == Error::None;
    }
};

/** Raw inputs and compensated project transport units. */
struct CompensatedSample {
    std::uint32_t raw_pressure_d1;
    std::uint32_t raw_temperature_d2;
    std::int32_t internal_temperature_milli_celsius;
    std::int32_t pressure_pascal;
};

/** Compensated sample plus calculation diagnostics. */
struct CompensationResult {
    Error error{Error::None};
    CompensatedSample value{};
    FirstOrderValues first_order{};
    SecondOrderCorrection second_order{};

    [[nodiscard]] constexpr bool ok() const noexcept {
        return error == Error::None;
    }
};

/** Maps a defined CSB level to its seven-bit address; floating is invalid. */
[[nodiscard]] constexpr AddressResult address_for_csb(
    const CsbLevel level) noexcept {
    switch (level) {
        case CsbLevel::Low:
            return {Error::None, kAddressCsbLow};
        case CsbLevel::High:
            return {Error::None, kAddressCsbHigh};
        case CsbLevel::Floating:
            return {Error::InvalidCsbLevel, 0U};
    }
    return {Error::InvalidCsbLevel, 0U};
}

/** Decodes one exactly-two-byte, MSB-first PROM response. */
[[nodiscard]] WordResult decode_prom_word(const std::uint8_t* data,
                                          std::size_t size) noexcept;
/** Decodes one exactly-three-byte, MSB-first ADC response and rejects zero. */
[[nodiscard]] AdcResult decode_adc(const std::uint8_t* data,
                                   std::size_t size) noexcept;

/**
 * Implements TE AN520's big-endian polynomial-0x3000 CRC-4 procedure.
 * The CRC byte in a local copy of word 7 is cleared; input remains unchanged.
 */
[[nodiscard]] std::uint8_t calculate_prom_crc4(
    const PromImage& prom) noexcept;
/** Requires matching CRC-4 and rejects all-zero/all-one C1..C6 sets. */
[[nodiscard]] PromValidationResult validate_prom(
    const PromImage& prom) noexcept;

/**
 * Calculates the exact MS5611 low- and very-low-temperature corrections.
 * Signed division truncates toward zero; negative signed shifts are not used.
 */
[[nodiscard]] SecondOrderResult calculate_second_order(
    std::int64_t temperature_delta,
    std::int64_t first_order_temperature_hundredths_celsius) noexcept;

/**
 * Applies complete first- and second-order compensation with 64-bit products.
 * Pressure is returned directly in Pa; die temperature is returned in milli-C.
 */
[[nodiscard]] CompensationResult compensate(
    const Calibration& calibration,
    std::uint32_t raw_pressure_d1,
    std::uint32_t raw_temperature_d2) noexcept;

}  // namespace shahbaz::sensors::ms5611
