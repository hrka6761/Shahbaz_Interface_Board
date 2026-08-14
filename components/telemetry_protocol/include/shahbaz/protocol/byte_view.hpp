#pragma once

/**
 * @file byte_view.hpp
 * @brief Non-owning byte ranges used by the allocation-free protocol API.
 */

#include <cstddef>
#include <cstdint>

namespace shahbaz::protocol {

/** @brief Read-only byte range; null is valid only when size is zero. */
struct ByteView final {
    const std::uint8_t* data{nullptr};
    std::size_t size{0U};
};

/** @brief Writable byte range; null is valid only when size is zero. */
struct MutableByteView final {
    std::uint8_t* data{nullptr};
    std::size_t size{0U};
};

/** @brief Checks the null/size invariant of a read-only byte range. */
[[nodiscard]] constexpr bool isValid(ByteView view) noexcept {
    return view.size == 0U || view.data != nullptr;
}

/** @brief Checks the null/size invariant of a writable byte range. */
[[nodiscard]] constexpr bool isValid(MutableByteView view) noexcept {
    return view.size == 0U || view.data != nullptr;
}

}  // namespace shahbaz::protocol
