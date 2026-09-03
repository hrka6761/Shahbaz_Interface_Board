/**
 * @file vl53l0x_domain.hpp
 * @brief Pure VL53L0X identities, result decoding, and project validity policy.
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace shahbaz::sensors::vl53l0x {

inline constexpr std::size_t kSensorCount = 4U;
inline constexpr std::uint8_t kDefaultAddress = 0x29U;
inline constexpr std::array<std::uint8_t, kSensorCount> kDefaultAssignedAddresses{
    {0x30U, 0x31U, 0x32U, 0x33U}};
inline constexpr std::uint16_t kExpectedModelId = 0xEEAAU;
inline constexpr std::uint16_t kMinimumControlDistanceMm = 30U;
inline constexpr std::uint16_t kMaximumControlDistanceMm = 2'000U;
inline constexpr std::uint32_t kMinimumTimingBudgetUs = 20'000U;
inline constexpr std::uint32_t kDefaultTimingBudgetUs = 33'000U;
inline constexpr std::uint8_t kFieldOfViewDegrees = 25U;

enum class Role : std::uint8_t {
    Ground = 0,
    Up = 1,
    FrontLeft = 2,
    FrontRight = 3,
};

[[nodiscard]] constexpr auto role_for_instance(const std::size_t instance) noexcept
    -> Role {
    switch (instance) {
        case 0U: return Role::Ground;
        case 1U: return Role::Up;
        case 2U: return Role::FrontLeft;
        case 3U: return Role::FrontRight;
        default: return Role::Ground;
    }
}

/** Raw status codes extracted from RESULT_RANGE_STATUS bits 6:3. */
enum class RangeStatus : std::uint8_t {
    Valid = 0U,
    SigmaFailure = 1U,
    SignalFailure = 2U,
    MinimumRangeFailure = 3U,
    PhaseFailure = 4U,
    HardwareFailure = 5U,
    RangeComplete = 11U,
    Unknown = 0xFFU,
};

enum class Error : std::uint8_t {
    None = 0,
    NullData,
    InvalidLength,
    UnexpectedModelId,
};

struct ModelIdResult final {
    Error error{Error::None};
    std::uint16_t value{};

    [[nodiscard]] constexpr auto ok() const noexcept -> bool {
        return error == Error::None;
    }
};

struct RangeReading final {
    std::uint16_t distance_mm{};
    std::uint8_t raw_status{};
    RangeStatus status{RangeStatus::Unknown};
    std::uint8_t signal_quality_percent{};
    bool status_valid{};
    bool distance_within_control_range{};

    [[nodiscard]] constexpr auto control_eligible() const noexcept -> bool {
        return status_valid && distance_within_control_range;
    }
};

struct RangeResult final {
    Error error{Error::None};
    RangeReading value{};

    [[nodiscard]] constexpr auto ok() const noexcept -> bool {
        return error == Error::None;
    }
};

[[nodiscard]] constexpr auto is_usable_seven_bit_address(
    const std::uint8_t address) noexcept -> bool {
    return address >= 0x08U && address <= 0x77U;
}

[[nodiscard]] constexpr auto decode_range_status(const std::uint8_t raw) noexcept
    -> RangeStatus {
    switch (raw) {
        case 0U: return RangeStatus::Valid;
        case 1U: return RangeStatus::SigmaFailure;
        case 2U: return RangeStatus::SignalFailure;
        case 3U: return RangeStatus::MinimumRangeFailure;
        case 4U: return RangeStatus::PhaseFailure;
        case 5U: return RangeStatus::HardwareFailure;
        case 11U: return RangeStatus::RangeComplete;
        default: return RangeStatus::Unknown;
    }
}

[[nodiscard]] constexpr auto is_valid_range_status(const std::uint8_t raw) noexcept
    -> bool {
    // PX4's VL53L0X driver accepts raw status 0 and 11. Shahbaz keeps the raw
    // code on the wire and applies the same conservative validity decision.
    return raw == 0U || raw == 11U;
}

[[nodiscard]] auto parse_model_id(const std::uint8_t* data, std::size_t size) noexcept
    -> ModelIdResult;

/** Parses the 12-byte block beginning at RESULT_RANGE_STATUS (0x14). */
[[nodiscard]] auto parse_range_result(const std::uint8_t* data, std::size_t size) noexcept
    -> RangeResult;

/**
 * Applies the factory SPAD count/type selection to the six-byte enable map.
 * Returns false for an impossible count; the input map is unchanged on failure.
 */
[[nodiscard]] auto apply_reference_spad_selection(
    std::array<std::uint8_t, 6U>& enable_map,
    std::uint8_t requested_count,
    bool aperture_spads) noexcept -> bool;

}  // namespace shahbaz::sensors::vl53l0x
