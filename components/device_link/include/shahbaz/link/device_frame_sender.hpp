#pragma once

#include "shahbaz/interfaces/monotonic_clock.hpp"
#include "shahbaz/interfaces/telemetry_transport.hpp"
#include "shahbaz/protocol/wire_protocol.hpp"

#include <atomic>
#include <cstdint>

namespace shahbaz::link {

class DeviceFrameSender final {
  public:
    DeviceFrameSender(interfaces::ITelemetryTransport& transport,
                      interfaces::IMonotonicClock& clock) noexcept
        : transport_(transport), clock_(clock) {}

    [[nodiscard]] auto send(protocol::MessageType type,
                            protocol::MessagePriority priority,
                            protocol::ByteView payload,
                            std::uint64_t lifetime_us = 250'000U) noexcept
        -> interfaces::TransportStatus;

    [[nodiscard]] auto nextSequenceForTest() const noexcept -> std::uint32_t {
        return sequence_.load(std::memory_order_relaxed);
    }

  private:
    interfaces::ITelemetryTransport& transport_;
    interfaces::IMonotonicClock& clock_;
    std::atomic<std::uint32_t> sequence_{0U};
};

}  // namespace shahbaz::link
