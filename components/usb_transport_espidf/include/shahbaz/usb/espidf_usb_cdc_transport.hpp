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
#include "freertos/queue.h"
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

/** Coherent physical/logical USB state sampled from one atomic value. */
struct UsbConnectionSnapshot final {
    std::uint32_t epoch{};
    bool mounted{};
    bool dtr_open{};

    [[nodiscard]] constexpr auto connected() const noexcept -> bool {
        return mounted && dtr_open;
    }
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
        return connectionSnapshot().connected();
    }
    [[nodiscard]] auto connectionSnapshot() const noexcept -> UsbConnectionSnapshot;
    /**
     * Change counter for physical mount and logical CDC-open boundaries. Callers
     * compare values and must not interpret the counter's magnitude.
     */
    [[nodiscard]] auto connectionEpoch() const noexcept -> std::uint32_t {
        return connectionSnapshot().epoch;
    }
    /** Admit transport traffic only while the coherent connection stays at epoch. */
    [[nodiscard]] auto admitSession(std::uint32_t epoch) noexcept -> bool;
    void revokeSession() noexcept;
    [[nodiscard]] auto sessionAdmitted(std::uint32_t epoch) const noexcept -> bool;
    [[nodiscard]] auto send(interfaces::ConstByteView frame,
                            interfaces::TransportPriority priority,
                            std::uint64_t expires_at_us) noexcept
        -> interfaces::TransportStatus override;

    /**
     * Clears RX/TX state at a physical or logical CDC-session boundary. Call
     * before a new ProtocolEngine session is admitted and after the link closes.
     */
    void resetSession() noexcept;
    /** Drains bytes already received by TinyUSB without blocking. */
    [[nodiscard]] auto read(interfaces::MutableByteView destination) noexcept -> std::size_t;
    /** Advances bounded TX work; call frequently from the application service loop. */
    void serviceTx() noexcept;
    [[nodiscard]] auto statistics() const noexcept -> UsbTransportStatistics;

  private:
    static constexpr std::size_t kRxChunkBytes = 256U;
    static constexpr std::size_t kRxQueueDepth = 8U;
    static constexpr std::size_t kTxQueueDepth = 12U;
    static constexpr std::uint32_t kMountedFlag = 1U << 0U;
    static constexpr std::uint32_t kDtrOpenFlag = 1U << 1U;
    static constexpr std::uint32_t kConnectionFlagMask = kMountedFlag | kDtrOpenFlag;
    static constexpr std::uint32_t kConnectionEpochIncrement = 1U << 2U;
    static constexpr std::uint32_t kTxBoundaryPendingFlag = 1U << 31U;

    struct RxChunk final {
        std::array<std::uint8_t, kRxChunkBytes> bytes{};
        std::uint32_t epoch{};
        std::uint16_t size{};
    };

    struct TxItem final {
        std::array<std::uint8_t, protocol::kMaxDelimitedFrameLength> bytes{};
        std::size_t size{};
        std::size_t offset{};
        std::uint64_t expires_at_us{};
        interfaces::TransportPriority priority{interfaces::TransportPriority::Normal};
        std::uint32_t order{};
        std::uint32_t epoch{};
        bool used{};
    };

    static void deviceEvent(tinyusb_event_t* event, void* arg);
    static void cdcRx(int interface_number, cdcacm_event_t* event);
    static void cdcLineStateChanged(int interface_number, cdcacm_event_t* event);
    void updateConnectionState(std::uint32_t affected_flags,
                               std::uint32_t desired_flags) noexcept;
    void markTxBoundaryPending() noexcept;
    void clearTinyUsbBuffers() noexcept;
    [[nodiscard]] auto connectionSettled(std::uint32_t epoch) const noexcept -> bool;
    void drainCdcRx() noexcept;
    [[nodiscard]] auto selectTx() noexcept -> TxItem*;
    void clearTx() noexcept;
    static void increment(std::atomic<std::uint32_t>& value) noexcept;

    interfaces::IMonotonicClock& clock_;
    // The epoch and flags share one atomic word so readers cannot pair a new
    // mounted/DTR value with an old epoch (or the inverse).
    std::atomic<std::uint32_t> connection_state_{0U};
    // Zero means no admitted protocol session; otherwise this stores epoch + 1.
    std::atomic<std::uint32_t> admitted_epoch_plus_one_{0U};
    // Boundary FIFO cleanup completes before this catches up to connection_state_.
    std::atomic<std::uint32_t> settled_epoch_plus_one_{1U};
    // Main claims this around TinyUSB TX writes. A connection callback marks the
    // high bit rather than blocking, forcing a symmetric post-write FIFO clear.
    std::atomic<std::uint32_t> tx_write_guard_{0U};
    StaticQueue_t rx_queue_control_{};
    std::array<std::uint8_t, kRxQueueDepth * sizeof(RxChunk)> rx_queue_storage_{};
    QueueHandle_t rx_queue_{};
    RxChunk active_rx_{};
    std::size_t active_rx_offset_{};
    bool active_rx_used_{};
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
