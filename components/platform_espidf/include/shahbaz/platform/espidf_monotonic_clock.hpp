/**
 * @file espidf_monotonic_clock.hpp
 * @brief ESP timer implementation of the domain monotonic-clock port.
 */
#pragma once

#include "shahbaz/interfaces/monotonic_clock.hpp"

namespace shahbaz::platform {

class EspIdfMonotonicClock final : public interfaces::IMonotonicClock {
  public:
    /** Returns microseconds since boot; it neither blocks nor uses wall time. */
    [[nodiscard]] auto now_us() const noexcept -> std::uint64_t override;
};

} // namespace shahbaz::platform

