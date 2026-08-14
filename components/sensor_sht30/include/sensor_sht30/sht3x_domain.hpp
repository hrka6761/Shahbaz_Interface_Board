/**
 * @file sht3x_domain.hpp
 * @brief Pure C++17 SHT3x commands, CRC, decoding, and unit conversions.
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace shahbaz::sensors::sht3x {

/** @name Supported addresses and device commands. */
///@{
inline constexpr std::uint8_t kDefaultAddress = 0x44U;
inline constexpr std::uint8_t kAlternateAddress = 0x45U;

inline constexpr std::uint16_t kSoftResetCommand = 0x30A2U;
inline constexpr std::uint16_t kReadStatusCommand = 0xF32DU;
inline constexpr std::uint16_t kClearStatusCommand = 0x3041U;
inline constexpr std::uint16_t kHeaterEnableCommand = 0x306DU;
inline constexpr std::uint16_t kHeaterDisableCommand = 0x3066U;
///@}

/**
 * @name Datasheet timing limits and safe project deadlines, in microseconds.
 *
 * Measurement maxima use the worst case across the full documented supply
 * range, even though this project requires a verified nominal 3.3 V rail.
 */
///@{
inline constexpr std::uint32_t kMinimumCommandSpacingUs = 1'000U;
inline constexpr std::uint32_t kSoftResetMaximumDurationUs = 1'500U;
inline constexpr std::uint32_t kDefaultSoftResetDeadlineUs = 2'000U;
inline constexpr std::uint32_t kDefaultHighRepeatabilityDeadlineUs = 18'000U;
///@}

inline constexpr std::uint8_t kCrcPolynomial = 0x31U;
inline constexpr std::uint8_t kCrcInitialValue = 0xFFU;

/** Measurement repeatability choices for no-clock-stretch single-shot mode. */
enum class Repeatability : std::uint8_t {
    Low,
    Medium,
    High,
};

/** Command and conversion timing for one repeatability setting. */
struct MeasurementProfile {
    std::uint16_t command;
    std::uint32_t typical_duration_us;
    std::uint32_t maximum_duration_us;
};

/**
 * Returns the command and datasheet timing for a repeatability setting.
 * @param repeatability Requested no-clock-stretch repeatability.
 */
[[nodiscard]] constexpr MeasurementProfile measurement_profile(
    Repeatability repeatability) noexcept {
    switch (repeatability) {
        case Repeatability::Low:
            return {0x2416U, 2'500U, 4'500U};
        case Repeatability::Medium:
            return {0x240BU, 4'500U, 6'500U};
        case Repeatability::High:
            return {0x2400U, 12'500U, 15'500U};
    }
    return {0U, 0U, 0U};
}

/** Domain decoding failures; transport failures belong to the I2C adapter. */
enum class Error : std::uint8_t {
    None,
    NullData,
    InvalidLength,
    TemperatureCrcMismatch,
    HumidityCrcMismatch,
    StatusCrcMismatch,
};

/** One atomically validated temperature and humidity sample. */
struct Sample {
    std::uint16_t raw_temperature;
    std::uint16_t raw_relative_humidity;
    std::int32_t ambient_temperature_milli_celsius;
    std::uint32_t relative_humidity_milli_percent;
    bool temperature_outside_specified_range;
};

/** Typed result returned by measurement parsing. */
struct SampleResult {
    Error error{Error::None};
    Sample value{};

    [[nodiscard]] constexpr bool ok() const noexcept {
        return error == Error::None;
    }
};

/** Supported fields from the CRC-protected SHT3x status word. */
struct Status {
    std::uint16_t raw_word;
    bool alert_pending;
    bool heater_enabled;
    bool relative_humidity_tracking_alert;
    bool temperature_tracking_alert;
    bool system_reset_detected;
    bool last_command_failed;
    bool last_write_checksum_failed;
};

/** Typed result returned by status parsing. */
struct StatusResult {
    Error error{Error::None};
    Status value{};

    [[nodiscard]] constexpr bool ok() const noexcept {
        return error == Error::None;
    }
};

/**
 * Calculates Sensirion CRC-8 (poly 0x31, init 0xFF, no reflection/xor-out).
 * @param data Input bytes; may be null only when size is zero.
 * @param size Number of bytes covered by the CRC.
 * @return Eight-bit CRC; empty input returns the configured initial value.
 */
[[nodiscard]] std::uint8_t crc8(const std::uint8_t* data,
                                std::size_t size) noexcept;

/**
 * Converts raw temperature using round-to-nearest, with half rounded upward.
 * The complete transfer function is retained; this function does not clamp.
 */
[[nodiscard]] std::int32_t temperature_milli_celsius(
    std::uint16_t raw_temperature) noexcept;
/** Converts raw RH to milli-percent with the same rounding policy. */
[[nodiscard]] std::uint32_t relative_humidity_milli_percent(
    std::uint16_t raw_relative_humidity) noexcept;

/**
 * Decodes a six-byte sample and rejects the whole pair if either CRC fails.
 * @param data Temperature word/CRC followed by humidity word/CRC.
 * @param size Must be exactly six.
 */
[[nodiscard]] SampleResult parse_measurement(const std::uint8_t* data,
                                             std::size_t size) noexcept;
/** Decodes and CRC-validates one three-byte status response. */
[[nodiscard]] StatusResult parse_status(const std::uint8_t* data,
                                        std::size_t size) noexcept;

}  // namespace shahbaz::sensors::sht3x
