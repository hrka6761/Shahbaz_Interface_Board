#include "shahbaz/protocol/crc32c.hpp"

namespace shahbaz::protocol {

std::uint32_t crc32c(ByteView bytes) noexcept {
    if (!isValid(bytes)) {
        return 0U;
    }

    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t i = 0U; i < bytes.size; ++i) {
        crc ^= bytes.data[i];
        for (std::uint8_t bit = 0U; bit < 8U; ++bit) {
            const std::uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0x82F63B78U & mask);
        }
    }
    return crc ^ 0xFFFFFFFFU;
}

}  // namespace shahbaz::protocol

