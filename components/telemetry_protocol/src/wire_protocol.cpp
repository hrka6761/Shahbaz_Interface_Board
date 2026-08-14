#include "shahbaz/protocol/wire_protocol.hpp"

#include "shahbaz/protocol/cobs.hpp"
#include "shahbaz/protocol/crc32c.hpp"

#include <array>

namespace shahbaz::protocol {
namespace {

constexpr std::size_t kVersionOffset = 0U;
constexpr std::size_t kHeaderLengthOffset = 1U;
constexpr std::size_t kMessageTypeOffset = 2U;
constexpr std::size_t kFlagsOffset = 4U;
constexpr std::size_t kPriorityOffset = 6U;
constexpr std::size_t kReservedOffset = 7U;
constexpr std::size_t kSequenceOffset = 8U;
constexpr std::size_t kTimestampOffset = 12U;
constexpr std::size_t kPayloadLengthOffset = 20U;

void writeLe16(std::uint8_t* output, std::uint16_t value) noexcept {
    output[0] = static_cast<std::uint8_t>(value & 0xFFU);
    output[1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

void writeLe32(std::uint8_t* output, std::uint32_t value) noexcept {
    for (std::size_t i = 0U; i < 4U; ++i) {
        output[i] = static_cast<std::uint8_t>((value >> (8U * i)) & 0xFFU);
    }
}

void writeLe64(std::uint8_t* output, std::uint64_t value) noexcept {
    for (std::size_t i = 0U; i < 8U; ++i) {
        output[i] = static_cast<std::uint8_t>((value >> (8U * i)) & 0xFFU);
    }
}

std::uint16_t readLe16(const std::uint8_t* input) noexcept {
    return static_cast<std::uint16_t>(input[0]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(input[1]) << 8U);
}

std::uint32_t readLe32(const std::uint8_t* input) noexcept {
    std::uint32_t value = 0U;
    for (std::size_t i = 0U; i < 4U; ++i) {
        value |= static_cast<std::uint32_t>(input[i]) << (8U * i);
    }
    return value;
}

std::uint64_t readLe64(const std::uint8_t* input) noexcept {
    std::uint64_t value = 0U;
    for (std::size_t i = 0U; i < 8U; ++i) {
        value |= static_cast<std::uint64_t>(input[i]) << (8U * i);
    }
    return value;
}

bool isValidPriority(std::uint8_t value) noexcept {
    return value <= static_cast<std::uint8_t>(MessagePriority::Low);
}

FrameStatus mapCobsFailure(CobsStatus status) noexcept {
    switch (status) {
        case CobsStatus::OutputTooSmall:
            return FrameStatus::OutputTooSmall;
        case CobsStatus::EmptyEncoding:
        case CobsStatus::MalformedEncoding:
            return FrameStatus::MalformedCobs;
        case CobsStatus::InvalidArgument:
            return FrameStatus::InvalidArgument;
        case CobsStatus::Ok:
            return FrameStatus::Ok;
    }
    return FrameStatus::MalformedCobs;
}

bool parseMessageType(std::uint16_t raw, MessageType& output) noexcept {
    if (!isKnownMessageType(raw)) {
        return false;
    }
    output = static_cast<MessageType>(raw);
    return true;
}

}  // namespace

bool isKnownMessageType(std::uint16_t raw_type) noexcept {
    switch (static_cast<MessageType>(raw_type)) {
        case MessageType::DeviceInfoRequest:
        case MessageType::DeviceInfoResponse:
        case MessageType::StartTelemetry:
        case MessageType::StopTelemetry:
        case MessageType::SetSensorRate:
        case MessageType::SensorSample:
        case MessageType::DeviceStatusRequest:
        case MessageType::DeviceStatusResponse:
        case MessageType::Heartbeat:
        case MessageType::HeartbeatAck:
        case MessageType::Ping:
        case MessageType::Pong:
        case MessageType::TimeSyncRequest:
        case MessageType::TimeSyncResponse:
        case MessageType::CommandAck:
        case MessageType::CommandNack:
        case MessageType::ProtocolError:
        case MessageType::SafetyState:
        case MessageType::EmergencyStop:
        case MessageType::Disarm:
        case MessageType::ArmRequest:
        case MessageType::ArmConfirm:
        case MessageType::ActuatorCommand:
        case MessageType::MotorCommand:
        case MessageType::ServoCommand:
        case MessageType::SetControlMode:
            return true;
    }
    return false;
}

MessagePolicy policyFor(MessageType type) noexcept {
    (void)type;
    return MessagePolicy::SupportedCurrentPhase;
}

FrameStatus encodeFrame(
    const FrameHeader& header,
    ByteView payload,
    EncodedFrame& output) noexcept {
    output.size = 0U;
    if (!isValid(payload)) {
        return FrameStatus::InvalidArgument;
    }
    if (payload.size > kMaxPayloadLength) {
        return FrameStatus::PayloadTooLarge;
    }
    if (header.version != kProtocolVersion) {
        return FrameStatus::UnsupportedVersion;
    }
    if (header.header_length != kHeaderLength) {
        return FrameStatus::InvalidHeaderLength;
    }
    if (header.reserved != 0U) {
        return FrameStatus::InvalidReservedField;
    }
    if (!isValidPriority(static_cast<std::uint8_t>(header.priority))) {
        return FrameStatus::InvalidPriority;
    }
    if (!isKnownMessageType(static_cast<std::uint16_t>(header.message_type))) {
        return FrameStatus::UnknownMessageType;
    }
    if (header.payload_length != payload.size) {
        return FrameStatus::LengthMismatch;
    }

    std::array<std::uint8_t, kMaxDecodedFrameLength> decoded{};
    decoded[kVersionOffset] = header.version;
    decoded[kHeaderLengthOffset] = header.header_length;
    writeLe16(decoded.data() + kMessageTypeOffset,
              static_cast<std::uint16_t>(header.message_type));
    writeLe16(decoded.data() + kFlagsOffset, header.flags);
    decoded[kPriorityOffset] = static_cast<std::uint8_t>(header.priority);
    decoded[kReservedOffset] = header.reserved;
    writeLe32(decoded.data() + kSequenceOffset, header.sequence);
    writeLe64(decoded.data() + kTimestampOffset, header.sender_monotonic_us);
    writeLe16(decoded.data() + kPayloadLengthOffset, header.payload_length);

    for (std::size_t i = 0U; i < payload.size; ++i) {
        decoded[static_cast<std::size_t>(kHeaderLength) + i] = payload.data[i];
    }

    const std::size_t without_crc = static_cast<std::size_t>(kHeaderLength) + payload.size;
    const std::uint32_t crc = crc32c({decoded.data(), without_crc});
    writeLe32(decoded.data() + without_crc, crc);
    const std::size_t decoded_size = without_crc + kCrcLength;

    const CobsResult cobs = cobsEncode(
        {decoded.data(), decoded_size},
        {output.bytes.data(), output.bytes.size() - 1U});
    if (cobs.status != CobsStatus::Ok) {
        return mapCobsFailure(cobs.status);
    }

    output.bytes[cobs.bytes_written] = kFrameDelimiter;
    output.size = cobs.bytes_written + 1U;
    return FrameStatus::Ok;
}

FrameStatus decodeFrame(ByteView encoded_without_delimiter, DecodedFrame& output) noexcept {
    output = DecodedFrame{};
    if (!isValid(encoded_without_delimiter)) {
        return FrameStatus::InvalidArgument;
    }
    if (encoded_without_delimiter.size > kMaxCobsEncodedFrameLength) {
        return FrameStatus::PayloadTooLarge;
    }
    for (std::size_t i = 0U; i < encoded_without_delimiter.size; ++i) {
        if (encoded_without_delimiter.data[i] == kFrameDelimiter) {
            return FrameStatus::UnexpectedDelimiter;
        }
    }

    std::array<std::uint8_t, kMaxDecodedFrameLength> decoded{};
    const CobsResult cobs = cobsDecode(
        encoded_without_delimiter,
        {decoded.data(), decoded.size()});
    if (cobs.status != CobsStatus::Ok) {
        return mapCobsFailure(cobs.status);
    }
    if (cobs.bytes_written < static_cast<std::size_t>(kHeaderLength) + kCrcLength) {
        return FrameStatus::FrameTooShort;
    }

    // Integrity covers the entire decoded body except its final CRC. Verify it
    // before interpreting any unauthenticated header field. The physical frame
    // boundary, not the claimed payload length, locates this checksum.
    const std::size_t crc_offset = cobs.bytes_written - kCrcLength;
    const std::uint32_t received_crc = readLe32(decoded.data() + crc_offset);
    const std::uint32_t calculated_crc = crc32c({decoded.data(), crc_offset});
    if (received_crc != calculated_crc) {
        return FrameStatus::CrcMismatch;
    }

    if (decoded[kVersionOffset] != kProtocolVersion) {
        return FrameStatus::UnsupportedVersion;
    }
    if (decoded[kHeaderLengthOffset] != kHeaderLength) {
        return FrameStatus::InvalidHeaderLength;
    }
    if (decoded[kReservedOffset] != 0U) {
        return FrameStatus::InvalidReservedField;
    }
    if (!isValidPriority(decoded[kPriorityOffset])) {
        return FrameStatus::InvalidPriority;
    }

    const std::uint16_t raw_message_type = readLe16(decoded.data() + kMessageTypeOffset);
    MessageType message_type{};
    if (!parseMessageType(raw_message_type, message_type)) {
        return FrameStatus::UnknownMessageType;
    }

    const std::uint16_t payload_length = readLe16(decoded.data() + kPayloadLengthOffset);
    if (payload_length > kMaxPayloadLength) {
        return FrameStatus::PayloadTooLarge;
    }
    const std::size_t expected_length =
        static_cast<std::size_t>(kHeaderLength) + payload_length + kCrcLength;
    if (cobs.bytes_written != expected_length) {
        return FrameStatus::LengthMismatch;
    }

    output.header.version = decoded[kVersionOffset];
    output.header.header_length = decoded[kHeaderLengthOffset];
    output.header.message_type = message_type;
    output.header.flags = readLe16(decoded.data() + kFlagsOffset);
    output.header.priority = static_cast<MessagePriority>(decoded[kPriorityOffset]);
    output.header.reserved = decoded[kReservedOffset];
    output.header.sequence = readLe32(decoded.data() + kSequenceOffset);
    output.header.sender_monotonic_us = readLe64(decoded.data() + kTimestampOffset);
    output.header.payload_length = payload_length;
    output.payload_size = payload_length;
    for (std::size_t i = 0U; i < output.payload_size; ++i) {
        output.payload[i] = decoded[static_cast<std::size_t>(kHeaderLength) + i];
    }

    return FrameStatus::Ok;
}

}  // namespace shahbaz::protocol
