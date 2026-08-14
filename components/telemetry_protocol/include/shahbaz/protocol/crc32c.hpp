#pragma once

/**
 * @file crc32c.hpp
 * @brief Allocation-free CRC-32C/Castagnoli calculation.
 */

#include "shahbaz/protocol/byte_view.hpp"

#include <cstdint>

namespace shahbaz::protocol {

/**
 * @brief Calculates standard reflected CRC-32C/Castagnoli.
 * @param bytes Input range. An empty range has CRC zero.
 * @return CRC using polynomial `0x82F63B78`, init and xorout `0xFFFFFFFF`.
 */
[[nodiscard]] std::uint32_t crc32c(ByteView bytes) noexcept;

}  // namespace shahbaz::protocol
