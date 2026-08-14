/**
 * @file fixed_priority_queue.hpp
 * @brief Fixed-capacity priority queue with deterministic low-priority eviction.
 *
 * The queue performs no allocation. It is deliberately not internally locked;
 * one application owner or an RTOS adapter must serialize access. A more urgent
 * item may evict the oldest item at the least urgent available priority, while
 * equal/lower-priority pushes are rejected when full.
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace shahbaz::application {

enum class QueuePriority : std::uint8_t {
    Critical = 0,
    High = 1,
    Normal = 2,
    Low = 3,
};

enum class QueuePushResult : std::uint8_t {
    Accepted = 0,
    AcceptedAfterLowerPriorityEviction,
    RejectedFull,
};

struct QueueStatistics final {
    std::uint32_t accepted{};
    std::uint32_t popped{};
    std::uint32_t rejected{};
    std::uint32_t evicted{};
    std::size_t high_water_mark{};
};

template <typename T, std::size_t Capacity> class FixedPriorityQueue final {
    static_assert(Capacity > 0U);
    static_assert(std::is_trivially_copyable<T>::value,
                  "FixedPriorityQueue items must be trivially copyable");
    static_assert(std::is_nothrow_default_constructible<T>::value,
                  "FixedPriorityQueue items must be nothrow default constructible");

  public:
    [[nodiscard]] auto push(const T& value, const QueuePriority priority) noexcept
        -> QueuePushResult {
        auto target = first_free_slot();
        auto result = QueuePushResult::Accepted;
        if (target == Capacity) {
            target = eviction_candidate(priority);
            if (target == Capacity) {
                saturating_increment(statistics_.rejected);
                return QueuePushResult::RejectedFull;
            }
            saturating_increment(statistics_.evicted);
            result = QueuePushResult::AcceptedAfterLowerPriorityEviction;
        } else {
            ++size_;
            if (size_ > statistics_.high_water_mark) {
                statistics_.high_water_mark = size_;
            }
        }

        slots_[target] = Slot{true, priority, next_order_++, value};
        saturating_increment(statistics_.accepted);
        return result;
    }

    [[nodiscard]] auto pop(T& output) noexcept -> bool {
        const auto target = next_pop_candidate();
        if (target == Capacity) {
            return false;
        }
        output = slots_[target].value;
        slots_[target].occupied = false;
        --size_;
        saturating_increment(statistics_.popped);
        return true;
    }

    [[nodiscard]] constexpr auto size() const noexcept -> std::size_t { return size_; }
    [[nodiscard]] constexpr auto empty() const noexcept -> bool { return size_ == 0U; }
    [[nodiscard]] constexpr auto statistics() const noexcept -> QueueStatistics {
        return statistics_;
    }

  private:
    struct Slot final {
        bool occupied{};
        QueuePriority priority{QueuePriority::Low};
        std::uint64_t order{};
        T value{};
    };

    static void saturating_increment(std::uint32_t& value) noexcept {
        if (value != std::numeric_limits<std::uint32_t>::max()) {
            ++value;
        }
    }

    [[nodiscard]] auto first_free_slot() const noexcept -> std::size_t {
        for (std::size_t index = 0U; index < Capacity; ++index) {
            if (!slots_[index].occupied) {
                return index;
            }
        }
        return Capacity;
    }

    [[nodiscard]] auto eviction_candidate(const QueuePriority incoming) const noexcept
        -> std::size_t {
        std::size_t candidate = Capacity;
        for (std::size_t index = 0U; index < Capacity; ++index) {
            const auto& slot = slots_[index];
            if (static_cast<std::uint8_t>(slot.priority) <=
                static_cast<std::uint8_t>(incoming)) {
                continue;
            }
            if (candidate == Capacity ||
                static_cast<std::uint8_t>(slot.priority) >
                    static_cast<std::uint8_t>(slots_[candidate].priority) ||
                (slot.priority == slots_[candidate].priority &&
                 slot.order < slots_[candidate].order)) {
                candidate = index;
            }
        }
        return candidate;
    }

    [[nodiscard]] auto next_pop_candidate() const noexcept -> std::size_t {
        std::size_t candidate = Capacity;
        for (std::size_t index = 0U; index < Capacity; ++index) {
            const auto& slot = slots_[index];
            if (!slot.occupied) {
                continue;
            }
            if (candidate == Capacity ||
                static_cast<std::uint8_t>(slot.priority) <
                    static_cast<std::uint8_t>(slots_[candidate].priority) ||
                (slot.priority == slots_[candidate].priority &&
                 slot.order < slots_[candidate].order)) {
                candidate = index;
            }
        }
        return candidate;
    }

    std::array<Slot, Capacity> slots_{};
    std::size_t size_{};
    std::uint64_t next_order_{};
    QueueStatistics statistics_{};
};

} // namespace shahbaz::application
