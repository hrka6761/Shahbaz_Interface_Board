/**
 * @file sample_publisher.hpp
 * @brief Non-owning publication port for validated sensor samples.
 */
#pragma once

#include "shahbaz/domain/measurement.hpp"

namespace shahbaz::interfaces {

class ISamplePublisher {
  public:
    virtual ~ISamplePublisher() = default;

    /**
     * Copies or consumes a sample without retaining the reference. The method
     * must be bounded and must report queue/backpressure failure.
     */
    [[nodiscard]] virtual auto publish(const domain::SensorSample& sample) noexcept -> bool = 0;
};

} // namespace shahbaz::interfaces

