/**
 * @file task_health_monitor.hpp
 * @brief Allocation-free liveness records for critical application tasks.
 *
 * A single supervisor-context owner calls all methods. Task adapters publish
 * timestamps through their bounded queue or synchronization primitive; this
 * domain object itself does not perform locking or access FreeRTOS.
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace shahbaz::health {

enum class TaskId : std::uint8_t {
    SafetySupervisor = 0,
    UsbRx,
    UsbTx,
    CommandDispatcher,
    SensorScheduler,
    TelemetryEncoder,
    Maintenance,
    Count,
};

constexpr std::size_t kTaskCount = static_cast<std::size_t>(TaskId::Count);

struct TaskHealthConfig final {
    bool enabled{};
    bool critical{};
    std::uint64_t maximum_silence_us{};
};

struct TaskHealthRecord final {
    bool seen{};
    std::uint64_t last_liveness_us{};
    std::uint32_t publication_count{};
};

struct TaskHealthSnapshot final {
    std::uint32_t unhealthy_mask{};
    std::uint32_t unseen_mask{};
    std::uint32_t critical_unhealthy_mask{};

    [[nodiscard]] constexpr auto all_critical_healthy() const noexcept -> bool {
        return critical_unhealthy_mask == 0U;
    }
};

class TaskHealthMonitor final {
  public:
    explicit constexpr TaskHealthMonitor(
        const std::array<TaskHealthConfig, kTaskCount>& config) noexcept
        : config_(config) {}

    /** Records one liveness publication; timestamps must use one monotonic clock. */
    void mark_alive(TaskId task, std::uint64_t now_us) noexcept;

    /** Evaluates all enabled records without blocking or allocating. */
    [[nodiscard]] auto evaluate(std::uint64_t now_us) const noexcept
        -> TaskHealthSnapshot;

    [[nodiscard]] auto record(TaskId task) const noexcept -> TaskHealthRecord;

  private:
    std::array<TaskHealthConfig, kTaskCount> config_{};
    std::array<TaskHealthRecord, kTaskCount> records_{};
};

} // namespace shahbaz::health

