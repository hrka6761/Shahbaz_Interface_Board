#include "shahbaz/protocol/frame_accumulator.hpp"

namespace shahbaz::protocol {

StreamResult FrameAccumulator::pushByte(
    std::uint8_t byte,
    DecodedFrame& completed_frame) noexcept {
    if (byte != kFrameDelimiter) {
        if (discarding_) {
            return {StreamEvent::None, FrameStatus::Ok};
        }
        if (size_ >= encoded_.size()) {
            size_ = 0U;
            discarding_ = true;
            return {StreamEvent::None, FrameStatus::Ok};
        }
        encoded_[size_++] = byte;
        return {StreamEvent::None, FrameStatus::Ok};
    }

    if (discarding_) {
        reset();
        return {StreamEvent::OversizeDiscarded, FrameStatus::PayloadTooLarge};
    }
    if (size_ == 0U) {
        return {StreamEvent::EmptyDelimiter, FrameStatus::Ok};
    }

    const FrameStatus status = decodeFrame({encoded_.data(), size_}, completed_frame);
    reset();
    if (status != FrameStatus::Ok) {
        return {StreamEvent::FrameRejected, status};
    }
    return {StreamEvent::FrameReady, FrameStatus::Ok};
}

void FrameAccumulator::reset() noexcept {
    size_ = 0U;
    discarding_ = false;
}

}  // namespace shahbaz::protocol

