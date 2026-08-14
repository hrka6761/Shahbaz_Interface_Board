/**
 * @file i2c_bus.hpp
 * @brief Bounded synchronous I2C transaction port owned by the sensor scheduler.
 *
 * The interface exposes no ESP-IDF handles. The caller owns all buffers for the
 * duration of `transfer`. Implementations must honor `timeout_us` and must not
 * retry indefinitely.
 */
#pragma once

#include "shahbaz/interfaces/byte_view.hpp"

#include <cstdint>

namespace shahbaz::interfaces {

enum class I2cStatus : std::uint8_t {
    Ok = 0,
    InvalidArgument,
    NotInitialized,
    NotFound,
    Timeout,
    BusError,
    ArbitrationLost,
    RecoveryFailed,
};

/** Electrical boundary between nonempty write and read phases. */
enum class I2cWriteReadBoundary : std::uint8_t {
    /** Complete the write with STOP, then issue a fresh START for the read. */
    StopThenStart = 0,
    /** Keep bus ownership and transition to the read with a repeated START. */
    RepeatedStart,
};

struct I2cTransaction final {
    std::uint8_t seven_bit_address{};
    ConstByteView write{};
    MutableByteView read{};
    std::uint32_t timeout_us{};
    // Used only when both phases are nonempty. It is explicit because sensor
    // command/read diagrams do not make STOP and repeated-START interchangeable.
    I2cWriteReadBoundary write_read_boundary{I2cWriteReadBoundary::StopThenStart};
};

class II2cBus {
  public:
    virtual ~II2cBus() = default;

    /**
     * Executes one bounded transaction in the caller's context.
     *
     * Implementations must honor `write_read_boundary`, apply `timeout_us` to
     * the whole operation (including both phases), and return no later than the
     * bound. A write-only or read-only operation ignores the boundary field.
     */
    [[nodiscard]] virtual auto transfer(const I2cTransaction& transaction) noexcept
        -> I2cStatus = 0;

    /**
     * Attempts one bounded electrical bus recovery operation.
     *
     * A successful implementation must release SDA, generate at least nine SCL
     * pulses when a target can be holding SDA low, generate a valid START then
     * STOP condition, reinitialize the controller if necessary, and verify the
     * bus is idle. It must return `RecoveryFailed` or another non-Ok status if
     * those postconditions cannot be established inside `timeout_us`.
     */
    [[nodiscard]] virtual auto recover(std::uint32_t timeout_us) noexcept -> I2cStatus = 0;
};

} // namespace shahbaz::interfaces
