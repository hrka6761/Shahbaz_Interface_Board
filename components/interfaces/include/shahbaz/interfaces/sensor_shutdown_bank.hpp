/**
 * @file sensor_shutdown_bank.hpp
 * @brief Platform-neutral control of independent active-low sensor shutdown lines.
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace shahbaz::interfaces {

enum class ShutdownStatus : std::uint8_t {
    Ok = 0,
    InvalidArgument,
    NotInitialized,
    HardwareError,
};

/**
 * Controls a fixed bank of sensor enable lines without exposing GPIO handles.
 *
 * `set_enabled(index, false)` must assert the corresponding active-low XSHUT
 * line. Implementations must fail closed: initialization and bulk-disable leave
 * every line asserted low if any requested pin cannot be configured safely.
 */
class ISensorShutdownBank {
  public:
    virtual ~ISensorShutdownBank() = default;

    [[nodiscard]] virtual auto channel_count() const noexcept -> std::size_t = 0;
    [[nodiscard]] virtual auto set_enabled(std::size_t index, bool enabled) noexcept
        -> ShutdownStatus = 0;
    [[nodiscard]] virtual auto disable_all() noexcept -> ShutdownStatus = 0;
};

}  // namespace shahbaz::interfaces
