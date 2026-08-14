/**
 * @file task_health_monitor.cpp
 * @brief Deterministic task-liveness evaluation implementation.
 */
#include "shahbaz/health/task_health_monitor.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>

namespace shahbaz::health {
namespace {

constexpr auto index_of(const TaskId task) noexcept -> std::size_t {
    return static_cast<std::size_t>(task);
}

constexpr auto bit_for(const std::size_t index) noexcept -> std::uint32_t {
    return static_cast<std::uint32_t>(1UL << index);
}

} // namespace

void TaskHealthMonitor::mark_alive(const TaskId task, const std::uint64_t now_us) noexcept {
    const auto index = index_of(task);
    if (index >= records_.size() || !config_[index].enabled) {
        return;
    }
    auto& record_value = records_[index];
    record_value.seen = true;
    record_value.last_liveness_us = now_us;
    if (record_value.publication_count != std::numeric_limits<std::uint32_t>::max()) {
        ++record_value.publication_count;
    }
}

auto TaskHealthMonitor::evaluate(const std::uint64_t now_us) const noexcept
    -> TaskHealthSnapshot {
    TaskHealthSnapshot snapshot{};
    for (std::size_t index = 0U; index < records_.size(); ++index) {
        const auto& config = config_[index];
        const auto& record_value = records_[index];
        if (!config.enabled) {
            continue;
        }

        bool unhealthy = false;
        if (!record_value.seen) {
            snapshot.unseen_mask |= bit_for(index);
            unhealthy = true;
        } else if (now_us < record_value.last_liveness_us) {
            unhealthy = true;
        } else {
            unhealthy = (now_us - record_value.last_liveness_us) > config.maximum_silence_us;
        }

        if (unhealthy) {
            snapshot.unhealthy_mask |= bit_for(index);
            if (config.critical) {
                snapshot.critical_unhealthy_mask |= bit_for(index);
            }
        }
    }
    return snapshot;
}

auto TaskHealthMonitor::record(const TaskId task) const noexcept -> TaskHealthRecord {
    const auto index = index_of(task);
    return index < records_.size() ? records_[index] : TaskHealthRecord{};
}

} // namespace shahbaz::health
