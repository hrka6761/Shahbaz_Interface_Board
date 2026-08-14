/**
 * @file espidf_monotonic_clock.cpp
 * @brief ESP-IDF monotonic-clock adapter implementation.
 */
#include "shahbaz/platform/espidf_monotonic_clock.hpp"

#include "esp_timer.h"

#include <cstdint>

namespace shahbaz::platform {

auto EspIdfMonotonicClock::now_us() const noexcept -> std::uint64_t {
    const auto signed_time = esp_timer_get_time();
    return signed_time > 0 ? static_cast<std::uint64_t>(signed_time) : 0U;
}

} // namespace shahbaz::platform

