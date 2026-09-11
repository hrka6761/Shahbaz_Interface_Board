#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>
using TickType_t = std::uint32_t;
using UBaseType_t = unsigned;
using BaseType_t = int;
constexpr BaseType_t pdTRUE = 1;
constexpr BaseType_t pdFALSE = 0;
struct StaticQueue_t {
    std::size_t capacity{};
    std::size_t item_size{};
    std::deque<std::vector<std::uint8_t>> items;
};
using QueueHandle_t = StaticQueue_t*;
