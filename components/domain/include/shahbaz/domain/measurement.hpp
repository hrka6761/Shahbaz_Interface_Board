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
#include <limits>
#include <type_traits>

namespace shahbaz::domain {

enum class SensorId : std::uint8_t {
    Sht3x = 1,
    Ms5611 = 2,
    Vl53l0x = 3,
};

enum class FieldId : std::uint8_t {
    AmbientTemperatureMilliCelsius = 1,
    RelativeHumidityMilliPercent = 2,
    CompensatedPressurePascal = 3,
    InternalTemperatureMilliCelsius = 4,
    DistanceMillimeters = 5,
    RangeStatus = 6,
    SignalQualityPercent = 7,
    AcquisitionTimeUncertaintyMicros = 8,
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

/**
 * Conservative monotonic bounds for one physical acquisition.
 *
 * `started_at_us` is captured before the command that starts a conversion and
 * `completed_at_us` after the successful result read. Protocol v2 carries one
 * timestamp rather than both bounds, so producers serialize the midpoint. The
 * half-width is serialized in unsigned field 8 and must be combined with link
 * clock-mapping uncertainty by the consumer. UINT32_MAX means unbounded or
 * unrepresentable uncertainty, never a claim of a finite upper bound.
 */
struct AcquisitionWindow final {
    std::uint64_t started_at_us{};
    std::uint64_t completed_at_us{};

    [[nodiscard]] constexpr auto ordered() const noexcept -> bool {
        return completed_at_us >= started_at_us;
    }

    [[nodiscard]] constexpr auto midpoint_us() const noexcept -> std::uint64_t {
        return ordered()
                   ? started_at_us + ((completed_at_us - started_at_us) / 2U)
                   : completed_at_us;
    }

    [[nodiscard]] constexpr auto maximum_error_us() const noexcept
        -> std::uint64_t {
        if (!ordered()) return std::numeric_limits<std::uint64_t>::max();
        const auto span = completed_at_us - started_at_us;
        return (span / 2U) + (span % 2U);
    }

    [[nodiscard]] constexpr auto wire_uncertainty_us() const noexcept
        -> std::uint32_t {
        constexpr auto maximum = std::numeric_limits<std::uint32_t>::max();
        const auto error = maximum_error_us();
        return error >= maximum ? maximum : static_cast<std::uint32_t>(error);
    }
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
    /** Midpoint of the producer's ordered acquisition window, never enqueue time. */
    std::uint64_t monotonic_timestamp_us{};
    ValidityFlag validity{ValidityFlag::None};
    QualityFlag quality{QualityFlag::None};
    std::uint32_t health_flags{};
    std::uint8_t field_count{};
    std::array<MeasurementField, kMaximumFields> fields{};
};

static_assert(std::is_trivially_copyable_v<MeasurementField>);
static_assert(std::is_trivially_copyable_v<AcquisitionWindow>);
static_assert(std::is_trivially_copyable_v<SensorSample>);

} // namespace shahbaz::domain
