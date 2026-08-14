/**
 * @file monotonic_clock.hpp
 * @brief Monotonic time port for deadlines, freshness, and deterministic tests.
 */
#pragma once

#include <cstdint>

namespace shahbaz::interfaces {

class IMonotonicClock {
  public:
    virtual ~IMonotonicClock() = default;
    [[nodiscard]] virtual auto now_us() const noexcept -> std::uint64_t = 0;
};

} // namespace shahbaz::interfaces

