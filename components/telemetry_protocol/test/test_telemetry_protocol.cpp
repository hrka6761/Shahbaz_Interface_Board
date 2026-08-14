#include "shahbaz/protocol/cobs.hpp"
#include "shahbaz/protocol/crc32c.hpp"
#include "shahbaz/protocol/frame_accumulator.hpp"
#include "shahbaz/protocol/wire_protocol.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

#define CHECK(condition)                                                               \
    do {                                                                               \
        if (!(condition)) {                                                            \
            std::fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__,   \
                         #condition);                                                  \
            return false;                                                              \
        }                                                                              \
    } while (false)

using namespace shahbaz::protocol;

void refreshTrailingCrc(std::uint8_t* bytes, const std::size_t size) {
    const std::size_t crc_offset = size - kCrcLength;
    const std::uint32_t crc = crc32c({bytes, crc_offset});
    for (std::size_t index = 0U; index < kCrcLength; ++index) {
        bytes[crc_offset + index] =
            static_cast<std::uint8_t>(crc >> (8U * index));
    }
}

bool testCrc32cKnownVector() {
    constexpr char input[] = "123456789";
    CHECK(crc32c({reinterpret_cast<const std::uint8_t*>(input), 9U}) == 0xE3069283U);
    CHECK(crc32c({nullptr, 0U}) == 0U);
    return true;
}

bool testCobsRoundTripsAndBounds() {
    constexpr std::uint8_t input[] = {0x11U, 0x00U, 0x22U, 0x00U, 0x00U, 0x33U};
    std::array<std::uint8_t, 32U> encoded{};
    std::array<std::uint8_t, 32U> decoded{};

    const CobsResult encoded_result =
        cobsEncode({input, sizeof(input)}, {encoded.data(), encoded.size()});
    CHECK(encoded_result.status == CobsStatus::Ok);
    for (std::size_t i = 0U; i < encoded_result.bytes_written; ++i) {
        CHECK(encoded[i] != 0U);
    }

    const CobsResult decoded_result = cobsDecode(
        {encoded.data(), encoded_result.bytes_written},
        {decoded.data(), decoded.size()});
    CHECK(decoded_result.status == CobsStatus::Ok);
    CHECK(decoded_result.bytes_written == sizeof(input));
    CHECK(std::memcmp(decoded.data(), input, sizeof(input)) == 0);

    std::array<std::uint8_t, 256U> boundary{};
    for (std::size_t i = 0U; i < boundary.size(); ++i) {
        boundary[i] = static_cast<std::uint8_t>((i % 253U) + 1U);
    }
    boundary.back() = 0U;
    std::array<std::uint8_t, 260U> boundary_encoded{};
    std::array<std::uint8_t, 256U> boundary_decoded{};
    const CobsResult boundary_encode = cobsEncode(
        {boundary.data(), boundary.size()},
        {boundary_encoded.data(), boundary_encoded.size()});
    CHECK(boundary_encode.status == CobsStatus::Ok);
    const CobsResult boundary_decode = cobsDecode(
        {boundary_encoded.data(), boundary_encode.bytes_written},
        {boundary_decoded.data(), boundary_decoded.size()});
    CHECK(boundary_decode.status == CobsStatus::Ok);
    CHECK(boundary_decode.bytes_written == boundary.size());
    CHECK(boundary_decoded == boundary);

    std::array<std::uint8_t, 1U> empty_encoded{};
    const CobsResult empty = cobsEncode({nullptr, 0U}, {empty_encoded.data(), 1U});
    CHECK(empty.status == CobsStatus::Ok);
    CHECK(empty.bytes_written == 1U);
    CHECK(empty_encoded[0] == 0x01U);

    constexpr std::uint8_t truncated_block[] = {0x03U, 0x11U};
    CHECK(cobsDecode({truncated_block, sizeof(truncated_block)},
                     {decoded.data(), decoded.size()})
              .status == CobsStatus::MalformedEncoding);
    constexpr std::uint8_t embedded_zero[] = {0x02U, 0x00U};
    CHECK(cobsDecode({embedded_zero, sizeof(embedded_zero)},
                     {decoded.data(), decoded.size()})
              .status == CobsStatus::MalformedEncoding);
    return true;
}

bool testGoldenFrameAndRoundTrip() {
    constexpr std::uint8_t payload[] = {0xAAU, 0x00U, 0x55U};
    const FrameHeader header = makeHeader(
        MessageType::Heartbeat,
        MessagePriority::High,
        0x01020304U,
        UINT64_C(0x0102030405060708),
        static_cast<std::uint16_t>(sizeof(payload)));
    EncodedFrame encoded{};
    CHECK(encodeFrame(header, {payload, sizeof(payload)}, encoded) == FrameStatus::Ok);

    // Independent revision-2 golden vector, including the final delimiter.
    constexpr std::uint8_t expected[] = {
        0x04U, 0x02U, 0x16U, 0x40U, 0x01U, 0x01U, 0x02U, 0x01U,
        0x0EU, 0x04U, 0x03U, 0x02U, 0x01U, 0x08U, 0x07U, 0x06U,
        0x05U, 0x04U, 0x03U, 0x02U, 0x01U, 0x03U, 0x02U, 0xAAU,
        0x06U, 0x55U, 0x0FU, 0x12U, 0x40U, 0x80U, 0x00U,
    };
    CHECK(encoded.size == sizeof(expected));
    CHECK(std::memcmp(encoded.bytes.data(), expected, sizeof(expected)) == 0);

    DecodedFrame decoded{};
    CHECK(decodeFrame({encoded.bytes.data(), encoded.size - 1U}, decoded) == FrameStatus::Ok);
    CHECK(decoded.header.message_type == MessageType::Heartbeat);
    CHECK(decoded.header.priority == MessagePriority::High);
    CHECK(decoded.header.sequence == 0x01020304U);
    CHECK(decoded.header.sender_monotonic_us == UINT64_C(0x0102030405060708));
    CHECK(decoded.payload_size == sizeof(payload));
    CHECK(std::memcmp(decoded.payload.data(), payload, sizeof(payload)) == 0);
    return true;
}

bool testFrameRejectionPaths() {
    constexpr std::uint8_t payload[] = {0xAAU, 0x00U, 0x55U};
    const FrameHeader header = makeHeader(
        MessageType::Ping,
        MessagePriority::High,
        7U,
        99U,
        static_cast<std::uint16_t>(sizeof(payload)));
    EncodedFrame encoded{};
    CHECK(encodeFrame(header, {payload, sizeof(payload)}, encoded) == FrameStatus::Ok);

    std::array<std::uint8_t, kMaxDecodedFrameLength> raw{};
    const CobsResult raw_result = cobsDecode(
        {encoded.bytes.data(), encoded.size - 1U},
        {raw.data(), raw.size()});
    CHECK(raw_result.status == CobsStatus::Ok);

    raw[kHeaderLength] ^= 0x01U;
    std::array<std::uint8_t, kMaxCobsEncodedFrameLength> corrupted{};
    const CobsResult corrupted_result = cobsEncode(
        {raw.data(), raw_result.bytes_written},
        {corrupted.data(), corrupted.size()});
    CHECK(corrupted_result.status == CobsStatus::Ok);
    DecodedFrame decoded{};
    CHECK(decodeFrame({corrupted.data(), corrupted_result.bytes_written}, decoded) ==
          FrameStatus::CrcMismatch);

    raw[kHeaderLength] ^= 0x01U;
    raw[0] = static_cast<std::uint8_t>(kProtocolVersion + 1U);
    // A corrupted, unauthenticated header is an integrity error regardless of
    // which semantic field happened to change.
    const CobsResult wrong_version = cobsEncode(
        {raw.data(), raw_result.bytes_written},
        {corrupted.data(), corrupted.size()});
    CHECK(wrong_version.status == CobsStatus::Ok);
    CHECK(decodeFrame({corrupted.data(), wrong_version.bytes_written}, decoded) ==
          FrameStatus::CrcMismatch);

    // Once CRC is deliberately recomputed, semantic version diagnostics apply.
    refreshTrailingCrc(raw.data(), raw_result.bytes_written);
    const CobsResult valid_wrong_version = cobsEncode(
        {raw.data(), raw_result.bytes_written},
        {corrupted.data(), corrupted.size()});
    CHECK(valid_wrong_version.status == CobsStatus::Ok);
    CHECK(decodeFrame({corrupted.data(), valid_wrong_version.bytes_written}, decoded) ==
          FrameStatus::UnsupportedVersion);

    raw[0] = kProtocolVersion;
    raw[2] = 0xFFU;
    raw[3] = 0x7FU;
    refreshTrailingCrc(raw.data(), raw_result.bytes_written);
    const CobsResult unknown_type = cobsEncode(
        {raw.data(), raw_result.bytes_written},
        {corrupted.data(), corrupted.size()});
    CHECK(unknown_type.status == CobsStatus::Ok);
    CHECK(decodeFrame({corrupted.data(), unknown_type.bytes_written}, decoded) ==
          FrameStatus::UnknownMessageType);

    std::array<std::uint8_t, kMaxPayloadLength + 1U> oversized_payload{};
    const FrameHeader oversized_header = makeHeader(
        MessageType::Ping,
        MessagePriority::High,
        1U,
        1U,
        static_cast<std::uint16_t>(oversized_payload.size()));
    CHECK(encodeFrame(oversized_header,
                      {oversized_payload.data(), oversized_payload.size()},
                      encoded) == FrameStatus::PayloadTooLarge);
    return true;
}

bool testMaximumFrame() {
    std::array<std::uint8_t, kMaxPayloadLength> payload{};
    for (std::size_t i = 0U; i < payload.size(); ++i) {
        payload[i] = static_cast<std::uint8_t>(i & 0xFFU);
    }
    const FrameHeader header = makeHeader(
        MessageType::SensorSample,
        MessagePriority::Normal,
        UINT32_MAX,
        UINT64_MAX,
        static_cast<std::uint16_t>(payload.size()));
    EncodedFrame encoded{};
    CHECK(encodeFrame(header, {payload.data(), payload.size()}, encoded) == FrameStatus::Ok);
    CHECK(encoded.size <= kMaxDelimitedFrameLength);
    CHECK(encoded.bytes[encoded.size - 1U] == kFrameDelimiter);

    DecodedFrame decoded{};
    CHECK(decodeFrame({encoded.bytes.data(), encoded.size - 1U}, decoded) == FrameStatus::Ok);
    CHECK(decoded.payload_size == payload.size());
    CHECK(decoded.payload == payload);
    return true;
}

bool testIncrementalAccumulationAndResynchronization() {
    const FrameHeader header = makeHeader(
        MessageType::DeviceStatusRequest,
        MessagePriority::High,
        3U,
        10U,
        0U);
    EncodedFrame encoded{};
    CHECK(encodeFrame(header, {nullptr, 0U}, encoded) == FrameStatus::Ok);

    FrameAccumulator accumulator{};
    DecodedFrame decoded{};
    StreamResult result{};
    for (std::size_t i = 0U; i < encoded.size; ++i) {
        result = accumulator.pushByte(encoded.bytes[i], decoded);
        if (i + 1U != encoded.size) {
            CHECK(result.event == StreamEvent::None);
        }
    }
    CHECK(result.event == StreamEvent::FrameReady);
    CHECK(decoded.header.message_type == MessageType::DeviceStatusRequest);

    result = accumulator.pushByte(kFrameDelimiter, decoded);
    CHECK(result.event == StreamEvent::EmptyDelimiter);

    for (std::size_t i = 0U; i < kMaxCobsEncodedFrameLength + 1U; ++i) {
        result = accumulator.pushByte(0x11U, decoded);
        CHECK(result.event == StreamEvent::None);
    }
    CHECK(accumulator.discardingOversizeFrame());
    result = accumulator.pushByte(kFrameDelimiter, decoded);
    CHECK(result.event == StreamEvent::OversizeDiscarded);
    CHECK(!accumulator.discardingOversizeFrame());

    for (std::size_t i = 0U; i < encoded.size; ++i) {
        result = accumulator.pushByte(encoded.bytes[i], decoded);
    }
    CHECK(result.event == StreamEvent::FrameReady);
    return true;
}

bool testPhysicalControlMessagesAreProtocolSupported() {
    constexpr MessageType rejected[] = {
        MessageType::ArmRequest,
        MessageType::ArmConfirm,
        MessageType::ActuatorCommand,
        MessageType::MotorCommand,
        MessageType::ServoCommand,
        MessageType::SetControlMode,
    };
    for (MessageType type : rejected) {
        CHECK(policyFor(type) == MessagePolicy::SupportedCurrentPhase);
    }
    CHECK(policyFor(MessageType::EmergencyStop) == MessagePolicy::SupportedCurrentPhase);
    CHECK(policyFor(MessageType::Disarm) == MessagePolicy::SupportedCurrentPhase);
    CHECK(policyFor(MessageType::Heartbeat) == MessagePolicy::SupportedCurrentPhase);
    return true;
}

}  // namespace

int main() {
    const bool passed = testCrc32cKnownVector() &&
                        testCobsRoundTripsAndBounds() &&
                        testGoldenFrameAndRoundTrip() &&
                        testFrameRejectionPaths() &&
                        testMaximumFrame() &&
                        testIncrementalAccumulationAndResynchronization() &&
                        testPhysicalControlMessagesAreProtocolSupported();
    return passed ? 0 : 1;
}
