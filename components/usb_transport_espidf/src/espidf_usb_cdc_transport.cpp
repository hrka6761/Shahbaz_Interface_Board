#include "shahbaz/usb/espidf_usb_cdc_transport.hpp"

#include "tinyusb_default_config.h"

#include <algorithm>
#include <limits>

namespace shahbaz::usb {

EspIdfUsbCdcTransport* EspIdfUsbCdcTransport::instance_ = nullptr;

void EspIdfUsbCdcTransport::increment(std::atomic<std::uint32_t>& value) noexcept {
    auto current = value.load(std::memory_order_relaxed);
    while (current != std::numeric_limits<std::uint32_t>::max() &&
           !value.compare_exchange_weak(current, current + 1U,
                                        std::memory_order_relaxed,
                                        std::memory_order_relaxed)) {}
}

auto EspIdfUsbCdcTransport::initialize() noexcept -> bool {
    if (instance_ != nullptr || rx_queue_ != nullptr) return false;
    rx_queue_ = xQueueCreateStatic(kRxQueueDepth, sizeof(RxChunk),
                                   rx_queue_storage_.data(), &rx_queue_control_);
    if (rx_queue_ == nullptr) return false;
    instance_ = this;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
    // The esp_tinyusb default initializer is a C compound-literal macro.
    tinyusb_config_t tusb_config = TINYUSB_DEFAULT_CONFIG(deviceEvent, this);
#pragma GCC diagnostic pop
    if (tinyusb_driver_install(&tusb_config) != ESP_OK) {
        instance_ = nullptr;
        rx_queue_ = nullptr;
        return false;
    }

    tinyusb_config_cdcacm_t cdc_config{};
    cdc_config.cdc_port = TINYUSB_CDC_ACM_0;
    cdc_config.callback_rx = cdcRx;
    cdc_config.callback_rx_wanted_char = nullptr;
    cdc_config.callback_line_state_changed = cdcLineStateChanged;
    cdc_config.callback_line_coding_changed = nullptr;
    if (tinyusb_cdcacm_init(&cdc_config) != ESP_OK) {
        (void)tinyusb_driver_uninstall();
        instance_ = nullptr;
        rx_queue_ = nullptr;
        return false;
    }
    return true;
}

void EspIdfUsbCdcTransport::deviceEvent(tinyusb_event_t* const event, void* const arg) {
    if (event == nullptr || arg == nullptr) return;
    auto* self = static_cast<EspIdfUsbCdcTransport*>(arg);
    switch (event->id) {
        case TINYUSB_EVENT_ATTACHED:
            self->updateConnectionState(kMountedFlag, kMountedFlag);
            break;
        case TINYUSB_EVENT_DETACHED:
            // USB unmount ends the logical CDC session even if TinyUSB does not
            // deliver a separate control-line-state callback.
            self->updateConnectionState(kConnectionFlagMask, 0U);
            break;
        default:
            break;
    }
}

void EspIdfUsbCdcTransport::updateConnectionState(const std::uint32_t affected_flags,
                                                   const std::uint32_t desired_flags) noexcept {
    auto observed = connection_state_.load(std::memory_order_relaxed);
    for (;;) {
        auto updated_flags =
            (observed & ~affected_flags) | (desired_flags & affected_flags);
        // A late/stale line-state callback must never leave DTR logically open
        // after physical unmount. Valid hosts assert DTR only after mount.
        if ((updated_flags & kMountedFlag) == 0U) updated_flags &= ~kDtrOpenFlag;
        if ((observed & kConnectionFlagMask) == (updated_flags & kConnectionFlagMask)) return;

        // Epoch and flags are published by the same compare/exchange. This is a
        // coherent snapshot even if physical and CDC callbacks ever interleave.
        const auto next_epoch =
            ((observed & ~kConnectionFlagMask) + kConnectionEpochIncrement) &
            ~kConnectionFlagMask;
        const auto desired = next_epoch | (updated_flags & kConnectionFlagMask);
        if (connection_state_.compare_exchange_weak(observed, desired,
                                                    std::memory_order_release,
                                                    std::memory_order_relaxed)) {
            // Clear TinyUSB's own FIFOs at the callback-side boundary. Main also
            // validates after every TX write, closing either race ordering.
            markTxBoundaryPending();
            clearTinyUsbBuffers();
            if (connection_state_.load(std::memory_order_acquire) == desired) {
                settled_epoch_plus_one_.store((desired >> 2U) + 1U,
                                              std::memory_order_release);
            }
            return;
        }
    }
}

auto EspIdfUsbCdcTransport::connectionSnapshot() const noexcept
    -> UsbConnectionSnapshot {
    const auto state = connection_state_.load(std::memory_order_acquire);
    return {state >> 2U,
            (state & kMountedFlag) != 0U,
            (state & kDtrOpenFlag) != 0U};
}

auto EspIdfUsbCdcTransport::admitSession(const std::uint32_t epoch) noexcept -> bool {
    const auto before = connectionSnapshot();
    if (before.epoch != epoch || !before.connected() || !connectionSettled(epoch)) return false;

    admitted_epoch_plus_one_.store(epoch + 1U, std::memory_order_release);
    const auto after = connectionSnapshot();
    if (after.epoch != epoch || !after.connected() || !connectionSettled(epoch)) {
        revokeSession();
        return false;
    }
    return true;
}

void EspIdfUsbCdcTransport::revokeSession() noexcept {
    admitted_epoch_plus_one_.store(0U, std::memory_order_release);
}

auto EspIdfUsbCdcTransport::sessionAdmitted(const std::uint32_t epoch) const noexcept -> bool {
    if (admitted_epoch_plus_one_.load(std::memory_order_acquire) != epoch + 1U) return false;
    const auto connection = connectionSnapshot();
    return connection.epoch == epoch && connection.connected() && connectionSettled(epoch);
}

auto EspIdfUsbCdcTransport::connectionSettled(const std::uint32_t epoch) const noexcept -> bool {
    return settled_epoch_plus_one_.load(std::memory_order_acquire) == epoch + 1U;
}

void EspIdfUsbCdcTransport::clearTinyUsbBuffers() noexcept {
    if (!tinyusb_cdcacm_initialized(TINYUSB_CDC_ACM_0)) return;
    tud_cdc_n_read_flush(static_cast<std::uint8_t>(TINYUSB_CDC_ACM_0));
    (void)tud_cdc_n_write_clear(static_cast<std::uint8_t>(TINYUSB_CDC_ACM_0));
}

void EspIdfUsbCdcTransport::markTxBoundaryPending() noexcept {
    auto guard = tx_write_guard_.load(std::memory_order_acquire);
    while (guard != 0U && (guard & kTxBoundaryPendingFlag) == 0U &&
           !tx_write_guard_.compare_exchange_weak(guard,
                                                  guard | kTxBoundaryPendingFlag,
                                                  std::memory_order_acq_rel,
                                                  std::memory_order_acquire)) {}
}

void EspIdfUsbCdcTransport::cdcLineStateChanged(const int interface_number,
                                                 cdcacm_event_t* const event) {
    if (interface_number != static_cast<int>(TINYUSB_CDC_ACM_0) || instance_ == nullptr ||
        event == nullptr || event->type != CDC_EVENT_LINE_STATE_CHANGED) {
        return;
    }
    const bool dtr_open = event->line_state_changed_data.dtr;
    instance_->updateConnectionState(kDtrOpenFlag, dtr_open ? kDtrOpenFlag : 0U);
}

void EspIdfUsbCdcTransport::cdcRx(const int interface_number, cdcacm_event_t*) {
    if (interface_number != static_cast<int>(TINYUSB_CDC_ACM_0) || instance_ == nullptr) return;
    instance_->drainCdcRx();
}

void EspIdfUsbCdcTransport::drainCdcRx() noexcept {
    if (rx_queue_ == nullptr) return;
    for (;;) {
        const auto before = connectionSnapshot();
        RxChunk chunk{};
        chunk.epoch = before.epoch;
        std::size_t received = 0U;
        if (tinyusb_cdcacm_read(TINYUSB_CDC_ACM_0,
                                chunk.bytes.data(), chunk.bytes.size(), &received) != ESP_OK ||
            received == 0U) {
            return;
        }
        const auto after = connectionSnapshot();
        if (after.epoch != before.epoch || !after.connected() ||
            !sessionAdmitted(before.epoch)) {
            // A callback that straddles reset/reopen is discarded before queueing.
            // A later queue send is still safe because the chunk carries its epoch.
            continue;
        }
        chunk.size = static_cast<std::uint16_t>(received);
        if (xQueueSend(rx_queue_, &chunk, 0U) == pdTRUE) {
            for (std::size_t i = 0U; i < received; ++i) increment(rx_bytes_);
        } else {
            for (std::size_t i = 0U; i < received; ++i) increment(rx_overflow_bytes_);
        }
    }
}

void EspIdfUsbCdcTransport::resetSession() noexcept {
    revokeSession();
    clearTx();
    active_rx_ = {};
    active_rx_offset_ = 0U;
    active_rx_used_ = false;

    // Never xQueueReset while the TinyUSB callback may be writing. Non-blocking
    // receives are safe with the single callback writer, and any late item is
    // epoch-tagged so it cannot enter the next admitted session.
    if (rx_queue_ != nullptr) {
        RxChunk discarded{};
        for (std::size_t i = 0U; i < kRxQueueDepth; ++i) {
            if (xQueueReceive(rx_queue_, &discarded, 0U) != pdTRUE) break;
        }
    }
    clearTinyUsbBuffers();
}

auto EspIdfUsbCdcTransport::read(const interfaces::MutableByteView destination) noexcept -> std::size_t {
    if (!interfaces::isValid(destination) || destination.size == 0U || rx_queue_ == nullptr) return 0U;
    const auto connection = connectionSnapshot();
    if (!sessionAdmitted(connection.epoch)) return 0U;

    for (std::size_t attempt = 0U; attempt <= kRxQueueDepth; ++attempt) {
        if (!active_rx_used_) {
            if (xQueueReceive(rx_queue_, &active_rx_, 0U) != pdTRUE) return 0U;
            active_rx_offset_ = 0U;
            active_rx_used_ = true;
        }
        if (active_rx_.epoch != connection.epoch ||
            !sessionAdmitted(connection.epoch)) {
            active_rx_ = {};
            active_rx_offset_ = 0U;
            active_rx_used_ = false;
            continue;
        }

        const auto remaining = static_cast<std::size_t>(active_rx_.size) - active_rx_offset_;
        const auto copied = std::min(destination.size, remaining);
        std::copy_n(active_rx_.bytes.data() + active_rx_offset_, copied, destination.data);
        active_rx_offset_ += copied;
        if (active_rx_offset_ == active_rx_.size) {
            active_rx_ = {};
            active_rx_offset_ = 0U;
            active_rx_used_ = false;
        }
        return copied;
    }
    return 0U;
}

auto EspIdfUsbCdcTransport::send(const interfaces::ConstByteView frame,
                                  const interfaces::TransportPriority priority,
                                  const std::uint64_t expires_at_us) noexcept
    -> interfaces::TransportStatus {
    if (!interfaces::isValid(frame) || frame.size == 0U) return interfaces::TransportStatus::InvalidArgument;
    if (frame.size > protocol::kMaxDelimitedFrameLength) return interfaces::TransportStatus::Oversize;
    const auto connection = connectionSnapshot();
    if (!sessionAdmitted(connection.epoch)) return interfaces::TransportStatus::Disconnected;
    if (expires_at_us <= clock_.now_us()) return interfaces::TransportStatus::Expired;

    auto* free_item = static_cast<TxItem*>(nullptr);
    for (auto& item : tx_) {
        if (!item.used) {
            free_item = &item;
            break;
        }
    }
    if (free_item == nullptr) {
        increment(tx_frames_dropped_);
        return interfaces::TransportStatus::QueueFull;
    }
    std::copy_n(frame.data, frame.size, free_item->bytes.data());
    free_item->size = frame.size;
    free_item->offset = 0U;
    free_item->expires_at_us = expires_at_us;
    free_item->priority = priority;
    free_item->order = insertion_order_++;
    free_item->epoch = connection.epoch;
    free_item->used = true;
    increment(tx_frames_accepted_);
    return interfaces::TransportStatus::Accepted;
}

auto EspIdfUsbCdcTransport::selectTx() noexcept -> TxItem* {
    TxItem* selected = nullptr;
    for (auto& item : tx_) {
        if (!item.used) continue;
        if (selected == nullptr || item.priority < selected->priority ||
            (item.priority == selected->priority && item.order < selected->order)) {
            selected = &item;
        }
    }
    return selected;
}

void EspIdfUsbCdcTransport::clearTx() noexcept {
    active_tx_ = nullptr;
    for (auto& item : tx_) item = {};
}

void EspIdfUsbCdcTransport::serviceTx() noexcept {
    const auto before = connectionSnapshot();
    if (!sessionAdmitted(before.epoch)) {
        clearTx();
        return;
    }
    if (active_tx_ == nullptr) active_tx_ = selectTx();
    if (active_tx_ == nullptr) return;
    if (active_tx_->epoch != before.epoch) {
        clearTx();
        clearTinyUsbBuffers();
        return;
    }

    const auto now = clock_.now_us();
    if (now >= active_tx_->expires_at_us) {
        increment(tx_expired_);
        active_tx_->used = false;
        active_tx_ = nullptr;
        return;
    }

    // Claim a non-blocking writer guard. Connection callbacks never wait for
    // main; they mark its high bit and clear TinyUSB's FIFO instead.
    const auto guard_value = before.epoch + 1U;
    std::uint32_t idle_guard = 0U;
    if (!tx_write_guard_.compare_exchange_strong(idle_guard, guard_value,
                                                 std::memory_order_acq_rel,
                                                 std::memory_order_acquire)) {
        clearTinyUsbBuffers();
        clearTx();
        return;
    }

    const auto guarded_connection = connectionSnapshot();
    if (guarded_connection.epoch != before.epoch || !guarded_connection.connected() ||
        !sessionAdmitted(before.epoch)) {
        std::uint32_t expected_guard = guard_value;
        if (!tx_write_guard_.compare_exchange_strong(expected_guard, 0U,
                                                     std::memory_order_acq_rel,
                                                     std::memory_order_acquire)) {
            clearTinyUsbBuffers();
            tx_write_guard_.store(0U, std::memory_order_release);
        }
        clearTx();
        return;
    }

    const auto remaining = active_tx_->size - active_tx_->offset;
    const auto written = tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0,
                                                    active_tx_->bytes.data() + active_tx_->offset,
                                                    remaining);
    if (written != 0U) {
        active_tx_->offset += written;
        (void)tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, 0U);
    }

    std::uint32_t expected_guard = guard_value;
    const bool boundary_during_write =
        !tx_write_guard_.compare_exchange_strong(expected_guard, 0U,
                                                 std::memory_order_acq_rel,
                                                 std::memory_order_acquire);
    const auto after = connectionSnapshot();
    if (boundary_during_write || after.epoch != before.epoch || !after.connected() ||
        !sessionAdmitted(before.epoch)) {
        // If the callback cleared before this write, clear again. If it starts
        // after guard release, its own FIFO clear supplies the other ordering.
        clearTinyUsbBuffers();
        tx_write_guard_.store(0U, std::memory_order_release);
        clearTx();
        return;
    }
    if (active_tx_->offset == active_tx_->size) {
        active_tx_->used = false;
        active_tx_ = nullptr;
    }
}

auto EspIdfUsbCdcTransport::statistics() const noexcept -> UsbTransportStatistics {
    return {rx_bytes_.load(std::memory_order_relaxed),
            rx_overflow_bytes_.load(std::memory_order_relaxed),
            tx_frames_accepted_.load(std::memory_order_relaxed),
            tx_frames_dropped_.load(std::memory_order_relaxed),
            tx_expired_.load(std::memory_order_relaxed)};
}

}  // namespace shahbaz::usb
