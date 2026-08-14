#pragma once

#include "shahbaz/interfaces/byte_view.hpp"
#include "shahbaz/interfaces/monotonic_clock.hpp"
#include "shahbaz/interfaces/telemetry_transport.hpp"
#include "shahbaz/protocol/wire_protocol.hpp"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
// ESP-IDF 5.4 and TinyUSB public headers intentionally use GNU extensions
// (include_next, flexible arrays, statement expressions, and anonymous structs).
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include "tinyusb.h"
#include "tinyusb_cdc_acm.h"
#pragma GCC diagnostic pop

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace shahbaz::usb {

struct UsbTransportStatistics final {
    std::uint32_t rx_bytes{};
    std::uint32_t rx_overflow_bytes{};
    std::uint32_t tx_frames_accepted{};
    std::uint32_t tx_frames_dropped{};
    std::uint32_t tx_expired{};
};

/** Native ESP32-S3 USB-OTG CDC-ACM transport for delimited protocol frames. */
class EspIdfUsbCdcTransport final : public interfaces::ITelemetryTransport {
  public:
    explicit EspIdfUsbCdcTransport(interfaces::IMonotonicClock& clock) noexcept : clock_(clock) {}
    ~EspIdfUsbCdcTransport() override = default;

    EspIdfUsbCdcTransport(const EspIdfUsbCdcTransport&) = delete;
    auto operator=(const EspIdfUsbCdcTransport&) -> EspIdfUsbCdcTransport& = delete;

    [[nodiscard]] auto initialize() noexcept -> bool;
    [[nodiscard]] auto connected() const noexcept -> bool override {
        return attached_.load(std::memory_order_acquire);
    }
    [[nodiscard]] auto send(interfaces::ConstByteView frame,
                            interfaces::TransportPriority priority,
                            std::uint64_t expires_at_us) noexcept
        -> interfaces::TransportStatus override;

    /**
     * Clears RX/TX state at a physical attachment boundary. Call before a new
     * ProtocolEngine session is admitted and immediately after detach.
     */
    void resetSession() noexcept;
    /** Drains bytes already received by TinyUSB without blocking. */
    [[nodiscard]] auto read(interfaces::MutableByteView destination) noexcept -> std::size_t;
    /** Advances bounded TX work; call frequently from the application service loop. */
    void serviceTx() noexcept;
    [[nodiscard]] auto statistics() const noexcept -> UsbTransportStatistics;

  private:
    static constexpr std::size_t kRxBufferBytes = 2048U;
    static constexpr std::size_t kTxQueueDepth = 12U;

    struct TxItem final {
        std::array<std::uint8_t, protocol::kMaxDelimitedFrameLength> bytes{};
        std::size_t size{};
        std::size_t offset{};
        std::uint64_t expires_at_us{};
        interfaces::TransportPriority priority{interfaces::TransportPriority::Normal};
        std::uint32_t order{};
        bool used{};
    };

    static void deviceEvent(tinyusb_event_t* event, void* arg);
    static void cdcRx(int interface_number, cdcacm_event_t* event);
    void drainCdcRx() noexcept;
    [[nodiscard]] auto selectTx() noexcept -> TxItem*;
    void clearTx() noexcept;
    static void increment(std::atomic<std::uint32_t>& value) noexcept;

    interfaces::IMonotonicClock& clock_;
    std::atomic<bool> attached_{false};
    StaticStreamBuffer_t rx_stream_storage_{};
    std::array<std::uint8_t, kRxBufferBytes> rx_storage_{};
    StreamBufferHandle_t rx_stream_{};
    std::array<TxItem, kTxQueueDepth> tx_{};
    TxItem* active_tx_{};
    std::uint32_t insertion_order_{};
    std::atomic<std::uint32_t> rx_bytes_{0U};
    std::atomic<std::uint32_t> rx_overflow_bytes_{0U};
    std::atomic<std::uint32_t> tx_frames_accepted_{0U};
    std::atomic<std::uint32_t> tx_frames_dropped_{0U};
    std::atomic<std::uint32_t> tx_expired_{0U};

    static EspIdfUsbCdcTransport* instance_;
};

}  // namespace shahbaz::usb
