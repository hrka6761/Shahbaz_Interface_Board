/**
 * @file telemetry_transport.hpp
 * @brief Bounded transport port independent of TinyUSB and FreeRTOS.
 */
#pragma once

#include "shahbaz/interfaces/byte_view.hpp"

#include <cstdint>

namespace shahbaz::interfaces {

enum class TransportPriority : std::uint8_t {
    Critical = 0,
    High = 1,
    Normal = 2,
    Low = 3,
};

enum class TransportStatus : std::uint8_t {
    Accepted = 0,
    Disconnected,
    QueueFull,
    Expired,
    Oversize,
    InvalidArgument,
};

class ITelemetryTransport {
  public:
    virtual ~ITelemetryTransport() = default;

    [[nodiscard]] virtual auto connected() const noexcept -> bool = 0;

    /** Copies a bounded frame; the caller retains ownership of the input bytes. */
    [[nodiscard]] virtual auto send(ConstByteView frame,
                                    TransportPriority priority,
                                    std::uint64_t expires_at_us) noexcept -> TransportStatus = 0;
};

} // namespace shahbaz::interfaces

