/**
 * @file vl53l0x_domain.cpp
 * @brief Allocation-free VL53L0X domain decoding.
 */
#include "sensor_vl53l0x/vl53l0x_domain.hpp"

namespace shahbaz::sensors::vl53l0x {

auto parse_model_id(const std::uint8_t* const data, const std::size_t size) noexcept
    -> ModelIdResult {
    if (data == nullptr) return {Error::NullData, 0U};
    if (size != 2U) return {Error::InvalidLength, 0U};
    const auto value = static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(data[0]) << 8U) |
        static_cast<std::uint16_t>(data[1]));
    return value == kExpectedModelId
               ? ModelIdResult{Error::None, value}
               : ModelIdResult{Error::UnexpectedModelId, value};
}

auto parse_range_result(const std::uint8_t* const data, const std::size_t size) noexcept
    -> RangeResult {
    if (data == nullptr) return {Error::NullData, {}};
    if (size != 12U) return {Error::InvalidLength, {}};

    const auto raw_status = static_cast<std::uint8_t>((data[0] & 0x78U) >> 3U);
    const auto distance_mm = static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(data[10]) << 8U) |
        static_cast<std::uint16_t>(data[11]));
    const bool status_valid = is_valid_range_status(raw_status);
    const bool distance_valid = distance_mm >= kMinimumControlDistanceMm &&
                                distance_mm <= kMaximumControlDistanceMm;
    return {
        Error::None,
        RangeReading{
            distance_mm,
            raw_status,
            decode_range_status(raw_status),
            static_cast<std::uint8_t>(status_valid && distance_valid ? 100U : 0U),
            status_valid,
            distance_valid,
        },
    };
}

auto apply_reference_spad_selection(
    std::array<std::uint8_t, 6U>& enable_map,
    const std::uint8_t requested_count,
    const bool aperture_spads) noexcept -> bool {
    if (requested_count == 0U || requested_count > 48U) return false;
    auto candidate = enable_map;
    const std::uint8_t first_spad = aperture_spads ? 12U : 0U;
    std::uint8_t enabled = 0U;
    for (std::uint8_t index = 0U; index < 48U; ++index) {
        const auto byte_index = static_cast<std::size_t>(index / 8U);
        const auto bit = static_cast<std::uint8_t>(1U << (index % 8U));
        if (index < first_spad || enabled == requested_count) {
            candidate[byte_index] = static_cast<std::uint8_t>(candidate[byte_index] & ~bit);
        } else if ((candidate[byte_index] & bit) != 0U) {
            ++enabled;
        }
    }
    if (enabled != requested_count) return false;
    enable_map = candidate;
    return true;
}

}  // namespace shahbaz::sensors::vl53l0x
