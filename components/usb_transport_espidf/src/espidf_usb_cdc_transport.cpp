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
    if (instance_ != nullptr || rx_stream_ != nullptr) return false;
    rx_stream_ = xStreamBufferCreateStatic(rx_storage_.size(), 1U,
                                           rx_storage_.data(), &rx_stream_storage_);
    if (rx_stream_ == nullptr) return false;
    instance_ = this;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
    // The esp_tinyusb default initializer is a C compound-literal macro.
    tinyusb_config_t tusb_config = TINYUSB_DEFAULT_CONFIG(deviceEvent, this);
#pragma GCC diagnostic pop
    if (tinyusb_driver_install(&tusb_config) != ESP_OK) {
        instance_ = nullptr;
        rx_stream_ = nullptr;
        return false;
    }

    tinyusb_config_cdcacm_t cdc_config{};
    cdc_config.cdc_port = TINYUSB_CDC_ACM_0;
    cdc_config.callback_rx = cdcRx;
    cdc_config.callback_rx_wanted_char = nullptr;
    cdc_config.callback_line_state_changed = nullptr;
    cdc_config.callback_line_coding_changed = nullptr;
    if (tinyusb_cdcacm_init(&cdc_config) != ESP_OK) {
        (void)tinyusb_driver_uninstall();
        instance_ = nullptr;
        rx_stream_ = nullptr;
        return false;
    }
    return true;
}

void EspIdfUsbCdcTransport::deviceEvent(tinyusb_event_t* const event, void* const arg) {
    if (event == nullptr || arg == nullptr) return;
    auto* self = static_cast<EspIdfUsbCdcTransport*>(arg);
    switch (event->id) {
        case TINYUSB_EVENT_ATTACHED:
            self->attached_.store(true, std::memory_order_release);
            break;
        case TINYUSB_EVENT_DETACHED:
            self->attached_.store(false, std::memory_order_release);
            break;
        default:
            break;
    }
}

void EspIdfUsbCdcTransport::cdcRx(const int interface_number, cdcacm_event_t*) {
    if (interface_number != static_cast<int>(TINYUSB_CDC_ACM_0) || instance_ == nullptr) return;
    instance_->drainCdcRx();
}

void EspIdfUsbCdcTransport::drainCdcRx() noexcept {
    if (rx_stream_ == nullptr) return;
    std::array<std::uint8_t, 256U> scratch{};
    for (;;) {
        std::size_t received = 0U;
        if (tinyusb_cdcacm_read(TINYUSB_CDC_ACM_0, scratch.data(), scratch.size(), &received) != ESP_OK ||
            received == 0U) {
            return;
        }
        if (!connected()) {
            // Bytes delivered after the detach event belong to no admitted
            // protocol session and must never survive into a reconnect.
            continue;
        }
        const auto stored = xStreamBufferSend(rx_stream_, scratch.data(), received, 0U);
        for (std::size_t i = 0U; i < stored; ++i) increment(rx_bytes_);
        for (std::size_t i = stored; i < received; ++i) increment(rx_overflow_bytes_);
    }
}

void EspIdfUsbCdcTransport::resetSession() noexcept {
    clearTx();
    if (rx_stream_ != nullptr) {
        (void)xStreamBufferReset(rx_stream_);
    }

    // TinyUSB owns an additional unread RX buffer. Drain and discard it so a
    // prior physical attachment cannot seed the next protocol session.
    std::array<std::uint8_t, 256U> scratch{};
    for (;;) {
        std::size_t received = 0U;
        if (tinyusb_cdcacm_read(TINYUSB_CDC_ACM_0, scratch.data(), scratch.size(), &received) != ESP_OK ||
            received == 0U) {
            break;
        }
    }
    if (rx_stream_ != nullptr) {
        (void)xStreamBufferReset(rx_stream_);
    }
}

auto EspIdfUsbCdcTransport::read(const interfaces::MutableByteView destination) noexcept -> std::size_t {
    if (!connected() || !interfaces::isValid(destination) || destination.size == 0U ||
        rx_stream_ == nullptr) return 0U;
    return xStreamBufferReceive(rx_stream_, destination.data, destination.size, 0U);
}

auto EspIdfUsbCdcTransport::send(const interfaces::ConstByteView frame,
                                  const interfaces::TransportPriority priority,
                                  const std::uint64_t expires_at_us) noexcept
    -> interfaces::TransportStatus {
    if (!interfaces::isValid(frame) || frame.size == 0U) return interfaces::TransportStatus::InvalidArgument;
    if (frame.size > protocol::kMaxDelimitedFrameLength) return interfaces::TransportStatus::Oversize;
    if (!connected()) return interfaces::TransportStatus::Disconnected;
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
    if (!connected()) {
        clearTx();
        return;
    }
    if (active_tx_ == nullptr) active_tx_ = selectTx();
    if (active_tx_ == nullptr) return;

    const auto now = clock_.now_us();
    if (now >= active_tx_->expires_at_us) {
        increment(tx_expired_);
        active_tx_->used = false;
        active_tx_ = nullptr;
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
