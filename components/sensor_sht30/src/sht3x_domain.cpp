/**
 * @file sht3x_domain.cpp
 * @brief Allocation-free implementation of the SHT3x sensor domain API.
 */
#include "sensor_sht30/sht3x_domain.hpp"

namespace shahbaz::sensors::sht3x {
namespace {

constexpr std::uint64_t rounded_divide(std::uint64_t numerator,
                                       std::uint64_t denominator) noexcept {
    return (numerator + (denominator / 2U)) / denominator;
}

constexpr std::uint16_t decode_word(const std::uint8_t msb,
                                    const std::uint8_t lsb) noexcept {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(msb) << 8U) |
        static_cast<std::uint16_t>(lsb));
}

}  // namespace

std::uint8_t crc8(const std::uint8_t* const data,
                  const std::size_t size) noexcept {
    if (data == nullptr && size != 0U) {
        return kCrcInitialValue;
    }

    std::uint8_t crc = kCrcInitialValue;
    for (std::size_t index = 0; index < size; ++index) {
        crc = static_cast<std::uint8_t>(crc ^ data[index]);
        for (std::uint8_t bit = 0U; bit < 8U; ++bit) {
            crc = (crc & 0x80U) != 0U
                      ? static_cast<std::uint8_t>((crc << 1U) ^ kCrcPolynomial)
                      : static_cast<std::uint8_t>(crc << 1U);
        }
    }
    return crc;
}

std::int32_t temperature_milli_celsius(
    const std::uint16_t raw_temperature) noexcept {
    constexpr std::uint64_t kScaleNumerator = 175'000U;
    constexpr std::uint64_t kRawDenominator = 65'535U;
    const auto scaled = rounded_divide(
        kScaleNumerator * static_cast<std::uint64_t>(raw_temperature),
        kRawDenominator);
    return static_cast<std::int32_t>(scaled) - 45'000;
}

std::uint32_t relative_humidity_milli_percent(
    const std::uint16_t raw_relative_humidity) noexcept {
    constexpr std::uint64_t kScaleNumerator = 100'000U;
    constexpr std::uint64_t kRawDenominator = 65'535U;
    return static_cast<std::uint32_t>(rounded_divide(
        kScaleNumerator *
            static_cast<std::uint64_t>(raw_relative_humidity),
        kRawDenominator));
}

SampleResult parse_measurement(const std::uint8_t* const data,
                               const std::size_t size) noexcept {
    if (data == nullptr) {
        return {Error::NullData, {}};
    }
    if (size != 6U) {
        return {Error::InvalidLength, {}};
    }
    if (crc8(data, 2U) != data[2]) {
        return {Error::TemperatureCrcMismatch, {}};
    }
    if (crc8(data + 3U, 2U) != data[5]) {
        return {Error::HumidityCrcMismatch, {}};
    }

    const auto raw_temperature = decode_word(data[0], data[1]);
    const auto raw_humidity = decode_word(data[3], data[4]);
    const auto converted_temperature =
        temperature_milli_celsius(raw_temperature);

    Sample sample{
        raw_temperature,
        raw_humidity,
        converted_temperature,
        relative_humidity_milli_percent(raw_humidity),
        converted_temperature < -40'000 || converted_temperature > 125'000,
    };
    return {Error::None, sample};
}

StatusResult parse_status(const std::uint8_t* const data,
                          const std::size_t size) noexcept {
    if (data == nullptr) {
        return {Error::NullData, {}};
    }
    if (size != 3U) {
        return {Error::InvalidLength, {}};
    }
    if (crc8(data, 2U) != data[2]) {
        return {Error::StatusCrcMismatch, {}};
    }

    const auto raw = decode_word(data[0], data[1]);
    Status status{
        raw,
        (raw & (1U << 15U)) != 0U,
        (raw & (1U << 13U)) != 0U,
        (raw & (1U << 11U)) != 0U,
        (raw & (1U << 10U)) != 0U,
        (raw & (1U << 4U)) != 0U,
        (raw & (1U << 1U)) != 0U,
        (raw & (1U << 0U)) != 0U,
    };
    return {Error::None, status};
}

}  // namespace shahbaz::sensors::sht3x
