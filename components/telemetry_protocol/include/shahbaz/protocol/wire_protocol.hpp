#pragma once

/**
 * @file wire_protocol.hpp
 * @brief Shahbaz version-2 binary header, message vocabulary, and bounded frame codec.
 *
 * Revision 2 is the current firmware/application interoperability contract.
 * It adds per-logical-CDC-session binding for state-changing host commands.
 * Production USB identifiers remain a deployment concern, not a wire-format field.
 */

#include "shahbaz/protocol/byte_view.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace shahbaz::protocol {

// Versioned wire revision. Incompatible future changes require a new version.
inline constexpr std::uint8_t kProtocolVersion = 2U;
inline constexpr std::uint8_t kHeaderLength = 22U;
inline constexpr std::size_t kMaxPayloadLength = 512U;
inline constexpr std::size_t kCrcLength = 4U;
inline constexpr std::size_t kMaxDecodedFrameLength =
    static_cast<std::size_t>(kHeaderLength) + kMaxPayloadLength + kCrcLength;
inline constexpr std::size_t kMaxCobsEncodedFrameLength =
    kMaxDecodedFrameLength + (kMaxDecodedFrameLength / 254U) + 1U;
inline constexpr std::size_t kMaxDelimitedFrameLength =
    kMaxCobsEncodedFrameLength + 1U;
inline constexpr std::uint8_t kFrameDelimiter = 0U;

/** @brief On-wire traffic class, ordered from safety critical to expendable. */
enum class MessagePriority : std::uint8_t {
    Critical = 0U,
    High = 1U,
    Normal = 2U,
    Low = 3U,
};

// Explicit revision-2 wire identifiers; numeric message IDs remain stable from v1.
enum class MessageType : std::uint16_t {
    DeviceInfoRequest = 0x0001U,
    DeviceInfoResponse = 0x0002U,
    StartTelemetry = 0x0010U,
    StopTelemetry = 0x0011U,
    SetSensorRate = 0x0012U,
    SensorSample = 0x0020U,
    DeviceStatusRequest = 0x0030U,
    DeviceStatusResponse = 0x0031U,
    Heartbeat = 0x0040U,
    HeartbeatAck = 0x0041U,
    Ping = 0x0042U,
    Pong = 0x0043U,
    TimeSyncRequest = 0x0050U,
    TimeSyncResponse = 0x0051U,
    CommandAck = 0x0060U,
    CommandNack = 0x0061U,
    ProtocolError = 0x0062U,
    SafetyState = 0x0063U,
    EmergencyStop = 0x0070U,
    Disarm = 0x0071U,

    // Physical-control messages. Execution is gated by synchronized time, a fresh
    // heartbeat, SafetySupervisor state, and actuator availability.
    ArmRequest = 0x8000U,
    ArmConfirm = 0x8001U,
    ActuatorCommand = 0x8010U,
    MotorCommand = 0x8011U,
    ServoCommand = 0x8012U,
    SetControlMode = 0x8013U,
};

/** @brief Current-phase authorization policy after integrity decoding. */
enum class MessagePolicy {
    SupportedCurrentPhase,
};

// Revision-2 negative acknowledgement values.
enum class NackReason : std::uint16_t {
    MalformedMessage = 0x0001U,
    UnsupportedVersion = 0x0002U,
    UnknownMessageType = 0x0003U,
    InvalidLength = 0x0004U,
    InvalidState = 0x0005U,
    DuplicateSequence = 0x0006U,
    OutOfOrderSequence = 0x0007U,
    StaleOrExpired = 0x0008U,
    QueueFull = 0x0009U,
    SessionMismatch = 0x000AU,
    ActuatorsNotImplemented = 0x0100U,  // Legacy reserved value; protocol v2 keeps it reserved and does not emit it.
};

/** @brief Host representation of the explicit 22-byte revision-2 header. */
struct FrameHeader final {
    std::uint8_t version{kProtocolVersion};
    std::uint8_t header_length{kHeaderLength};
    MessageType message_type{MessageType::ProtocolError};
    std::uint16_t flags{0U};
    MessagePriority priority{MessagePriority::Normal};
    std::uint8_t reserved{0U};
    std::uint32_t sequence{0U};
    std::uint64_t sender_monotonic_us{0U};
    std::uint16_t payload_length{0U};
};

/** @brief Fixed-capacity decoded frame owned by the caller. */
struct DecodedFrame final {
    FrameHeader header{};
    std::array<std::uint8_t, kMaxPayloadLength> payload{};
    std::size_t payload_size{0U};
};

/** @brief Fixed-capacity COBS frame including its final zero delimiter. */
struct EncodedFrame final {
    std::array<std::uint8_t, kMaxDelimitedFrameLength> bytes{};
    std::size_t size{0U};
};

/** @brief Integrity/format result from frame encoding or decoding. */
enum class FrameStatus {
    Ok,
    InvalidArgument,
    PayloadTooLarge,
    OutputTooSmall,
    MalformedCobs,
    FrameTooShort,
    UnsupportedVersion,
    InvalidHeaderLength,
    InvalidReservedField,
    InvalidPriority,
    UnknownMessageType,
    LengthMismatch,
    CrcMismatch,
    MissingDelimiter,
    UnexpectedDelimiter,
};

/**
 * @brief Builds a revision-2 header without relying on C++ object layout.
 * @param type Explicit revision-2 message identifier.
 * @param priority Traffic priority.
 * @param sequence Sender sequence number.
 * @param sender_monotonic_us Sender monotonic timestamp, never wall time.
 * @param payload_length Exact serialized payload length.
 * @param flags Revision-2 bit field; currently zero for all defined messages.
 */
[[nodiscard]] constexpr FrameHeader makeHeader(
    MessageType type,
    MessagePriority priority,
    std::uint32_t sequence,
    std::uint64_t sender_monotonic_us,
    std::uint16_t payload_length,
    std::uint16_t flags = 0U) noexcept {
    return FrameHeader{kProtocolVersion,
                       kHeaderLength,
                       type,
                       flags,
                       priority,
                       0U,
                       sequence,
                       sender_monotonic_us,
                       payload_length};
}

/** @brief Returns true only for an explicitly enumerated revision-2 message ID. */
[[nodiscard]] bool isKnownMessageType(std::uint16_t raw_type) noexcept;
/** @brief Returns the mandatory current-phase disposition for a known message. */
[[nodiscard]] MessagePolicy policyFor(MessageType type) noexcept;

/**
 * @brief Serializes, CRC-protects, COBS-encodes, and delimits one bounded frame.
 * @param header Header whose declared payload length must match `payload.size`.
 * @param payload Payload of at most `kMaxPayloadLength` bytes.
 * @param output Fixed-capacity destination replaced on success.
 */
[[nodiscard]] FrameStatus encodeFrame(
    const FrameHeader& header,
    ByteView payload,
    EncodedFrame& output) noexcept;

/**
 * @brief Decodes and integrity-checks exactly one delimiter-free COBS body.
 * @param encoded_without_delimiter One frame body; embedded zero is rejected.
 * @param output Fixed-capacity decoded frame replaced during processing.
 */
[[nodiscard]] FrameStatus decodeFrame(
    ByteView encoded_without_delimiter,
    DecodedFrame& output) noexcept;

}  // namespace shahbaz::protocol
