#pragma once

#include <cstdint>
#include <limits>

namespace shahbaz::actuator {

[[nodiscard]] constexpr auto pulse_us_to_duty(const std::uint16_t pulse_us,
                                               const std::uint32_t frequency_hz,
                                               const unsigned resolution_bits) noexcept
    -> std::uint32_t {
    if (pulse_us == 0U || frequency_hz == 0U || resolution_bits == 0U || resolution_bits >= 31U) {
        return 0U;
    }
    const auto levels = UINT32_C(1) << resolution_bits;
    const auto numerator = static_cast<std::uint64_t>(pulse_us) * frequency_hz * levels;
    auto duty = static_cast<std::uint32_t>((numerator + 500'000ULL) / 1'000'000ULL);
    if (duty >= levels) duty = levels - 1U;
    return duty;
}

}  // namespace shahbaz::actuator
