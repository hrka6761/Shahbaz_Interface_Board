#include "shahbaz/link/device_frame_sender.hpp"

#include <limits>

namespace shahbaz::link {

auto DeviceFrameSender::send(const protocol::MessageType type,
                             const protocol::MessagePriority priority,
                             const protocol::ByteView payload,
                             const std::uint64_t lifetime_us) noexcept
    -> interfaces::TransportStatus {
    if (!protocol::isValid(payload) || payload.size > protocol::kMaxPayloadLength) {
        return interfaces::TransportStatus::InvalidArgument;
    }
    if (!transport_.connected()) {
        return interfaces::TransportStatus::Disconnected;
    }
    const auto now_us = clock_.now_us();
    const auto sequence = sequence_.fetch_add(1U, std::memory_order_relaxed);
    protocol::EncodedFrame frame{};
    const auto header = protocol::makeHeader(
        type, priority, sequence, now_us, static_cast<std::uint16_t>(payload.size));
    if (protocol::encodeFrame(header, payload, frame) != protocol::FrameStatus::Ok) {
        return interfaces::TransportStatus::InvalidArgument;
    }
    const auto max = std::numeric_limits<std::uint64_t>::max();
    const auto expires = lifetime_us > max - now_us ? max : now_us + lifetime_us;
    interfaces::TransportPriority transport_priority = interfaces::TransportPriority::Normal;
    switch (priority) {
        case protocol::MessagePriority::Critical:
            transport_priority = interfaces::TransportPriority::Critical;
            break;
        case protocol::MessagePriority::High:
            transport_priority = interfaces::TransportPriority::High;
            break;
        case protocol::MessagePriority::Normal:
            transport_priority = interfaces::TransportPriority::Normal;
            break;
        case protocol::MessagePriority::Low:
            transport_priority = interfaces::TransportPriority::Low;
            break;
    }
    return transport_.send({frame.bytes.data(), frame.size}, transport_priority, expires);
}

}  // namespace shahbaz::link
