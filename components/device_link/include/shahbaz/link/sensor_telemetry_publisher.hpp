#pragma once

#include "shahbaz/interfaces/sample_publisher.hpp"
#include "shahbaz/link/device_frame_sender.hpp"

#include <atomic>
#include <cstdint>

namespace shahbaz::link {

struct TelemetryStatistics final {
    std::uint32_t samples_seen{};
    std::uint32_t samples_sent{};
    std::uint32_t samples_dropped{};
};

class SensorTelemetryPublisher final : public interfaces::ISamplePublisher {
  public:
    explicit SensorTelemetryPublisher(DeviceFrameSender& sender) noexcept
        : sender_(sender) {}

    [[nodiscard]] auto publish(const domain::SensorSample& sample) noexcept -> bool override;
    void setEnabled(bool enabled) noexcept { enabled_.store(enabled, std::memory_order_release); }
    [[nodiscard]] auto enabled() const noexcept -> bool {
        return enabled_.load(std::memory_order_acquire);
    }
    [[nodiscard]] auto statistics() const noexcept -> TelemetryStatistics;

  private:
    static void increment(std::atomic<std::uint32_t>& value) noexcept;

    DeviceFrameSender& sender_;
    std::atomic<bool> enabled_{false};
    std::atomic<std::uint32_t> samples_seen_{0U};
    std::atomic<std::uint32_t> samples_sent_{0U};
    std::atomic<std::uint32_t> samples_dropped_{0U};
};

}  // namespace shahbaz::link
