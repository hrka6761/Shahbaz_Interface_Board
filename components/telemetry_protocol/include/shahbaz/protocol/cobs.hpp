#pragma once

/**
 * @file cobs.hpp
 * @brief Bounded Consistent Overhead Byte Stuffing encode/decode functions.
 */

#include "shahbaz/protocol/byte_view.hpp"

#include <cstddef>

namespace shahbaz::protocol {

/** @brief Result category for a COBS operation. */
enum class CobsStatus {
    Ok,
    InvalidArgument,
    EmptyEncoding,
    MalformedEncoding,
    OutputTooSmall,
};

/** @brief Status and valid output length returned by a COBS operation. */
struct CobsResult final {
    CobsStatus status{CobsStatus::InvalidArgument};
    std::size_t bytes_written{0U};
};

/**
 * @brief Encodes one byte range without adding a frame delimiter.
 * @param input Bytes to encode; an empty view is accepted.
 * @param output Caller-owned bounded destination.
 * @return Status and byte count. Empty input encodes as one `0x01` byte.
 */
[[nodiscard]] CobsResult cobsEncode(ByteView input, MutableByteView output) noexcept;

/**
 * @brief Decodes one COBS body after its delimiter has been removed.
 * @param input Nonempty COBS body; a zero within the body is malformed.
 * @param output Caller-owned bounded destination.
 * @return Status and decoded byte count.
 */
[[nodiscard]] CobsResult cobsDecode(ByteView input, MutableByteView output) noexcept;

}  // namespace shahbaz::protocol
