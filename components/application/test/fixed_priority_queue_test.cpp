/**
 * @file fixed_priority_queue_test.cpp
 * @brief Host tests for deterministic priority and eviction behavior.
 */
#include "shahbaz/application/fixed_priority_queue.hpp"

#include <cstdlib>
#include <iostream>

namespace {

auto expect(const bool condition, const char* const message) -> int {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        return 1;
    }
    return 0;
}

} // namespace

int main() {
    using shahbaz::application::FixedPriorityQueue;
    using shahbaz::application::QueuePriority;
    using shahbaz::application::QueuePushResult;

    int failures = 0;
    FixedPriorityQueue<int, 3U> queue{};
    failures += expect(queue.push(1, QueuePriority::Low) == QueuePushResult::Accepted,
                       "first low item accepted");
    failures += expect(queue.push(2, QueuePriority::Normal) == QueuePushResult::Accepted,
                       "normal item accepted");
    failures += expect(queue.push(3, QueuePriority::High) == QueuePushResult::Accepted,
                       "high item accepted");
    failures += expect(queue.push(4, QueuePriority::Critical) ==
                           QueuePushResult::AcceptedAfterLowerPriorityEviction,
                       "critical item evicts low item");
    failures += expect(queue.push(5, QueuePriority::Low) == QueuePushResult::RejectedFull,
                       "low item cannot evict urgent traffic");

    int output = 0;
    failures += expect(queue.pop(output) && output == 4, "critical pops first");
    failures += expect(queue.pop(output) && output == 3, "high pops second");
    failures += expect(queue.pop(output) && output == 2, "normal pops third");
    failures += expect(!queue.pop(output), "empty queue reports false");

    const auto statistics = queue.statistics();
    failures += expect(statistics.evicted == 1U && statistics.rejected == 1U,
                       "statistics record eviction and rejection");
    failures += expect(statistics.high_water_mark == 3U, "high-water mark is bounded");

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

