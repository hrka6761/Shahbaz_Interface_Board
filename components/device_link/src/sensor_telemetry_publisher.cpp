#include "shahbaz/link/sensor_telemetry_publisher.hpp"

#include <array>
#include <limits>

namespace shahbaz::link {
namespace {

void put32(std::uint8_t* out, const std::uint32_t value) noexcept {
    for (std::size_t i = 0U; i < 4U; ++i) {
        out[i] = static_cast<std::uint8_t>((value >> (8U * i)) & 0xFFU);
    }
}
void put64(std::uint8_t* out, const std::uint64_t value) noexcept {
    for (std::size_t i = 0U; i < 8U; ++i) {
        out[i] = static_cast<std::uint8_t>((value >> (8U * i)) & 0xFFU);
    }
}

}  // namespace

void SensorTelemetryPublisher::increment(std::atomic<std::uint32_t>& value) noexcept {
    auto current = value.load(std::memory_order_relaxed);
    while (current != std::numeric_limits<std::uint32_t>::max() &&
           !value.compare_exchange_weak(current, current + 1U,
                                        std::memory_order_relaxed,
                                        std::memory_order_relaxed)) {
    }
}

auto SensorTelemetryPublisher::publish(const domain::SensorSample& sample) noexcept -> bool {
    increment(samples_seen_);
    if (sample.field_count > domain::SensorSample::kMaximumFields) {
        increment(samples_dropped_);
        return false;
    }
    if (!enabled()) {
        return true;
    }

    // Fixed SensorSample payload (unchanged in protocol v2): sensor, instance, sequence, timestamp, validity,
    // quality, health, field_count, then field tuples <BBI>.
    std::array<std::uint8_t, 64U> payload{};
    std::size_t offset = 0U;
    payload[offset++] = static_cast<std::uint8_t>(sample.sensor_id);
    payload[offset++] = sample.instance_id;
    put32(payload.data() + offset, sample.sequence);
    offset += 4U;
    put64(payload.data() + offset, sample.monotonic_timestamp_us);
    offset += 8U;
    put32(payload.data() + offset, static_cast<std::uint32_t>(sample.validity));
    offset += 4U;
    put32(payload.data() + offset, static_cast<std::uint32_t>(sample.quality));
    offset += 4U;
    put32(payload.data() + offset, sample.health_flags);
    offset += 4U;
    payload[offset++] = sample.field_count;
    for (std::size_t index = 0U; index < sample.field_count; ++index) {
        const auto& field = sample.fields[index];
        payload[offset++] = static_cast<std::uint8_t>(field.id);
        payload[offset++] = static_cast<std::uint8_t>(field.type);
        std::uint32_t wire_value = 0U;
        if (field.type == domain::FieldType::Signed32) {
            if (field.value < INT32_MIN || field.value > INT32_MAX) {
                increment(samples_dropped_);
                return false;
            }
            const auto signed_value = static_cast<std::int32_t>(field.value);
            wire_value = static_cast<std::uint32_t>(signed_value);
        } else if (field.type == domain::FieldType::Unsigned32) {
            if (field.value < 0 || static_cast<std::uint64_t>(field.value) > UINT32_MAX) {
                increment(samples_dropped_);
                return false;
            }
            wire_value = static_cast<std::uint32_t>(field.value);
        } else {
            increment(samples_dropped_);
            return false;
        }
        put32(payload.data() + offset, wire_value);
        offset += 4U;
    }
    const auto status = sender_.send(protocol::MessageType::SensorSample,
                                     protocol::MessagePriority::Normal,
                                     {payload.data(), offset});
    if (status == interfaces::TransportStatus::Accepted) {
        increment(samples_sent_);
        return true;
    }
    increment(samples_dropped_);
    return false;
}

auto SensorTelemetryPublisher::statistics() const noexcept -> TelemetryStatistics {
    return {samples_seen_.load(std::memory_order_relaxed),
            samples_sent_.load(std::memory_order_relaxed),
            samples_dropped_.load(std::memory_order_relaxed)};
}

}  // namespace shahbaz::link
