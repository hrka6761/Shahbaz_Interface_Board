/**
 * @file byte_view.hpp
 * @brief Allocation-free byte spans for C++17 component boundaries.
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace shahbaz::interfaces {

struct ConstByteView final {
    const std::uint8_t* data{};
    std::size_t size{};
};

struct MutableByteView final {
    std::uint8_t* data{};
    std::size_t size{};
};

[[nodiscard]] constexpr auto isValid(const ConstByteView view) noexcept -> bool {
    return view.size == 0U || view.data != nullptr;
}

[[nodiscard]] constexpr auto isValid(const MutableByteView view) noexcept -> bool {
    return view.size == 0U || view.data != nullptr;
}

} // namespace shahbaz::interfaces

