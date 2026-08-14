/**
 * @file measurement.hpp
 * @brief Fixed-size, transport-independent sensor measurement value types.
 *
 * This domain header intentionally has no ESP-IDF, RTOS, concrete-sensor, or
 * transport dependency. Values are explicit integers so units and signedness
 * survive queueing and serialization without floating-point ambiguity.
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace shahbaz::domain {

enum class SensorId : std::uint8_t {
    Sht3x = 1,
    Ms5611 = 2,
};

enum class FieldId : std::uint8_t {
    AmbientTemperatureMilliCelsius = 1,
    RelativeHumidityMilliPercent = 2,
    CompensatedPressurePascal = 3,
    InternalTemperatureMilliCelsius = 4,
};

enum class FieldType : std::uint8_t {
    Signed32 = 1,
    Unsigned32 = 2,
};

enum class ValidityFlag : std::uint32_t {
    None = 0,
    TransportValid = 1U << 0U,
    CrcValid = 1U << 1U,
    CalibrationValid = 1U << 2U,
    TimingValid = 1U << 3U,
    PlausibilityValid = 1U << 4U,
};

enum class QualityFlag : std::uint32_t {
    None = 0,
    Fresh = 1U << 0U,
    RecoveredAfterError = 1U << 1U,
    RateLimited = 1U << 2U,
};

[[nodiscard]] constexpr auto operator|(const ValidityFlag lhs,
                                       const ValidityFlag rhs) noexcept -> ValidityFlag {
    return static_cast<ValidityFlag>(static_cast<std::uint32_t>(lhs) |
                                     static_cast<std::uint32_t>(rhs));
}

[[nodiscard]] constexpr auto operator|(const QualityFlag lhs,
                                       const QualityFlag rhs) noexcept -> QualityFlag {
    return static_cast<QualityFlag>(static_cast<std::uint32_t>(lhs) |
                                    static_cast<std::uint32_t>(rhs));
}

struct MeasurementField final {
    FieldId id{};
    FieldType type{};
    // A signed 64-bit carrier represents every signed/unsigned 32-bit field
    // exactly. `type` selects the validated 32-bit wire encoding.
    std::int64_t value{};
};

[[nodiscard]] constexpr auto make_signed_field(const FieldId id,
                                               const std::int32_t value) noexcept
    -> MeasurementField {
    return {id, FieldType::Signed32, static_cast<std::int64_t>(value)};
}

[[nodiscard]] constexpr auto make_unsigned_field(const FieldId id,
                                                 const std::uint32_t value) noexcept
    -> MeasurementField {
    return {id, FieldType::Unsigned32, static_cast<std::int64_t>(value)};
}

/**
 * @brief A fixed-capacity sample suitable for deterministic RTOS queues.
 *
 * The producer owns construction. Queue/transport consumers receive a copy.
 * The type performs no allocation and has no blocking behavior.
 */
struct SensorSample final {
    static constexpr std::size_t kMaximumFields = 4U;

    SensorId sensor_id{};
    std::uint8_t instance_id{};
    std::uint32_t sequence{};
    std::uint64_t monotonic_timestamp_us{};
    ValidityFlag validity{ValidityFlag::None};
    QualityFlag quality{QualityFlag::None};
    std::uint32_t health_flags{};
    std::uint8_t field_count{};
    std::array<MeasurementField, kMaximumFields> fields{};
};

static_assert(std::is_trivially_copyable_v<MeasurementField>);
static_assert(std::is_trivially_copyable_v<SensorSample>);

} // namespace shahbaz::domain
