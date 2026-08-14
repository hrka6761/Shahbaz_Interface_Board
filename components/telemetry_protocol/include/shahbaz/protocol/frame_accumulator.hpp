#pragma once

/**
 * @file frame_accumulator.hpp
 * @brief Incremental, bounded, delimiter-driven protocol frame accumulation.
 */

#include "shahbaz/protocol/wire_protocol.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace shahbaz::protocol {

/** @brief Observable outcome after one input byte is consumed. */
enum class StreamEvent {
    None,
    FrameReady,
    EmptyDelimiter,
    FrameRejected,
    OversizeDiscarded,
};

/** @brief Stream event plus detailed codec result for rejection events. */
struct StreamResult final {
    StreamEvent event{StreamEvent::None};
    FrameStatus status{FrameStatus::Ok};
};

/**
 * @brief Reassembles frames from arbitrarily fragmented input without allocation.
 *
 * After overflow, bytes are discarded until the next delimiter, providing
 * deterministic parser resynchronization. One instance is intended for one RX
 * stream and must be externally serialized.
 */
class FrameAccumulator final {
public:
    /**
     * @brief Consumes one byte and optionally publishes one complete frame.
     * @param byte Next transport byte.
     * @param completed_frame Replaced only when a frame reaches decode processing.
     * @return Whether more data is needed, a frame is ready/rejected, or resync occurred.
     */
    [[nodiscard]] StreamResult pushByte(
        std::uint8_t byte,
        DecodedFrame& completed_frame) noexcept;

    /** @brief Drops buffered bytes and ends oversize-discard mode. */
    void reset() noexcept;

    /** @brief Returns the number of encoded bytes currently buffered. */
    [[nodiscard]] std::size_t bufferedSize() const noexcept { return size_; }
    /** @brief Reports whether input is being discarded until the next delimiter. */
    [[nodiscard]] bool discardingOversizeFrame() const noexcept { return discarding_; }

private:
    std::array<std::uint8_t, kMaxCobsEncodedFrameLength> encoded_{};
    std::size_t size_{0U};
    bool discarding_{false};
};

}  // namespace shahbaz::protocol
