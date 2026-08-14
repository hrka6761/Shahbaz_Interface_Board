/**
 * @file task_health_monitor_test.cpp
 * @brief Host tests for liveness deadlines and critical-task classification.
 */
#include "shahbaz/health/task_health_monitor.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace {

using shahbaz::health::TaskHealthConfig;
using shahbaz::health::TaskHealthMonitor;
using shahbaz::health::TaskId;

auto config() -> std::array<TaskHealthConfig, shahbaz::health::kTaskCount> {
    std::array<TaskHealthConfig, shahbaz::health::kTaskCount> value{};
    value[static_cast<std::size_t>(TaskId::SafetySupervisor)] = {true, true, 1'000U};
    value[static_cast<std::size_t>(TaskId::SensorScheduler)] = {true, true, 2'000U};
    value[static_cast<std::size_t>(TaskId::Maintenance)] = {true, false, 10'000U};
    return value;
}

auto expect(const bool condition, const char* const message) -> int {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        return 1;
    }
    return 0;
}

} // namespace

int main() {
    int failures = 0;
    TaskHealthMonitor monitor{config()};

    auto snapshot = monitor.evaluate(0U);
    failures += expect(!snapshot.all_critical_healthy(), "unseen critical tasks are unhealthy");

    monitor.mark_alive(TaskId::SafetySupervisor, 1'000U);
    monitor.mark_alive(TaskId::SensorScheduler, 1'000U);
    monitor.mark_alive(TaskId::Maintenance, 1'000U);
    snapshot = monitor.evaluate(2'000U);
    failures += expect(snapshot.all_critical_healthy(), "deadline boundary remains healthy");

    snapshot = monitor.evaluate(2'001U);
    failures += expect(!snapshot.all_critical_healthy(), "expired safety task is unhealthy");

    monitor.mark_alive(TaskId::SafetySupervisor, 3'000U);
    snapshot = monitor.evaluate(2'999U);
    failures += expect(!snapshot.all_critical_healthy(), "clock regression is unhealthy");

    const auto record = monitor.record(TaskId::SafetySupervisor);
    failures += expect(record.seen && record.publication_count == 2U,
                       "liveness record counts publications");

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

