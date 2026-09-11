#include "shahbaz/usb/espidf_usb_cdc_transport.hpp"
#include "shahbaz/protocol/frame_accumulator.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

namespace {
namespace usb = shahbaz::usb;
namespace protocol = shahbaz::protocol;
namespace interfaces = shahbaz::interfaces;
int failures = 0;
#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #condition); ++failures; } } while (false)

struct FakeUsb {
    tinyusb_config_t device{};
    tinyusb_config_cdcacm_t cdc{};
    std::vector<std::uint8_t> wire;
    std::vector<std::uint8_t> rx;
    std::size_t write_capacity{std::numeric_limits<std::size_t>::max()};
    std::size_t synthetic_rx_chunks{};
    std::size_t read_calls{};
    std::size_t write_calls{};
    bool detach_during_write{};
} fake;

struct Clock final : interfaces::IMonotonicClock {
    std::uint64_t now{100U};
    auto now_us() const noexcept -> std::uint64_t override { return now; }
};

void deviceEvent(const int id) {
    tinyusb_event_t event{id};
    fake.device.callback(&event, fake.device.argument);
}

void lineState(const bool open) {
    cdcacm_event_t event{};
    event.type = CDC_EVENT_LINE_STATE_CHANGED;
    event.line_state_changed_data.dtr = open;
    fake.cdc.callback_line_state_changed(0, &event);
}

void reset(usb::EspIdfUsbCdcTransport& transport, Clock& clock) {
    transport.resetSession();
    clock.now = 100U;
    fake.wire.clear();
    fake.rx.clear();
    fake.synthetic_rx_chunks = 0U;
    fake.read_calls = 0U;
    fake.write_calls = 0U;
    fake.write_capacity = std::numeric_limits<std::size_t>::max();
    fake.detach_during_write = false;
    deviceEvent(TINYUSB_EVENT_ATTACHED);
    lineState(true);
    CHECK(transport.admitSession(transport.connectionEpoch()));
}

auto frame(const std::uint32_t sequence) -> protocol::EncodedFrame {
    protocol::EncodedFrame encoded{};
    CHECK(protocol::encodeFrame(protocol::makeHeader(protocol::MessageType::HeartbeatAck,
        protocol::MessagePriority::Critical, sequence, 100U, 0U), {}, encoded) ==
        protocol::FrameStatus::Ok);
    return encoded;
}

auto decodeSequences() -> std::vector<std::uint32_t> {
    protocol::FrameAccumulator accumulator;
    protocol::DecodedFrame decoded;
    std::vector<std::uint32_t> sequences;
    for (const auto byte : fake.wire) {
        if (accumulator.pushByte(byte, decoded).event == protocol::StreamEvent::FrameReady) {
            sequences.push_back(decoded.header.sequence);
        }
    }
    return sequences;
}

void send(usb::EspIdfUsbCdcTransport& transport, const protocol::EncodedFrame& encoded,
          const std::uint64_t expires, const interfaces::TransportPriority priority =
              interfaces::TransportPriority::Normal) {
    CHECK(transport.send({encoded.bytes.data(), encoded.size}, priority, expires) ==
          interfaces::TransportStatus::Accepted);
}

void expiredPrefixPreservesNextFrame(usb::EspIdfUsbCdcTransport& transport, Clock& clock) {
    reset(transport, clock);
    const auto expired = frame(1U);
    const auto fresh = frame(2U);
    send(transport, expired, 150U);
    fake.write_capacity = 4U;
    transport.serviceTx();
    CHECK(fake.wire.size() == 4U);
    clock.now = 150U;
    fake.write_capacity = 0U; // Even the resynchronization delimiter must tolerate backpressure.
    transport.serviceTx();
    send(transport, fresh, 1'000U, interfaces::TransportPriority::Critical);
    CHECK(fake.wire.size() == 4U);
    fake.write_capacity = std::numeric_limits<std::size_t>::max();
    transport.serviceTx();
    transport.serviceTx();
    CHECK(decodeSequences() == std::vector<std::uint32_t>{2U});
    CHECK(fake.wire.size() == 4U + 1U + fresh.size);
}

void expiredQueuedFramesReleaseCapacity(usb::EspIdfUsbCdcTransport& transport, Clock& clock) {
    reset(transport, clock);
    const auto expired_before = transport.statistics().tx_expired;
    const auto encoded = frame(3U);
    std::size_t queued = 0U;
    while (queued < 100U && transport.send({encoded.bytes.data(), encoded.size},
        interfaces::TransportPriority::Normal, 150U) == interfaces::TransportStatus::Accepted) ++queued;
    CHECK(queued > 0U && queued < 100U);
    clock.now = 150U;
    const auto fresh = frame(4U);
    send(transport, fresh, 1'000U, interfaces::TransportPriority::Critical);
    transport.serviceTx();
    CHECK(decodeSequences() == std::vector<std::uint32_t>{4U});
    CHECK(transport.statistics().tx_expired - expired_before == queued);
}

void priorityDoesNotInterleaveFrames(usb::EspIdfUsbCdcTransport& transport, Clock& clock) {
    reset(transport, clock);
    send(transport, frame(5U), 1'000U, interfaces::TransportPriority::Low);
    send(transport, frame(6U), 1'000U, interfaces::TransportPriority::Critical);
    fake.write_capacity = 4U;
    transport.serviceTx();
    send(transport, frame(7U), 1'000U, interfaces::TransportPriority::Critical);
    fake.write_capacity = std::numeric_limits<std::size_t>::max();
    for (unsigned step = 0U; step < 4U; ++step) transport.serviceTx();
    const std::vector<std::uint32_t> expected{6U, 7U, 5U};
    CHECK(decodeSequences() == expected);
}

void reconnectDuringWriteDiscardsOldSession(usb::EspIdfUsbCdcTransport& transport, Clock& clock) {
    reset(transport, clock);
    const auto prior_epoch = transport.connectionEpoch();
    send(transport, frame(8U), 1'000U);
    fake.detach_during_write = true;
    transport.serviceTx();
    CHECK(!transport.sessionAdmitted(prior_epoch));
    reset(transport, clock);
    CHECK(transport.connectionEpoch() != prior_epoch);
    send(transport, frame(9U), 1'000U);
    transport.serviceTx();
    CHECK(decodeSequences() == std::vector<std::uint32_t>{9U});
}

void receiveFloodHasBoundedCallbackWork(usb::EspIdfUsbCdcTransport& transport, Clock& clock) {
    reset(transport, clock);
    fake.synthetic_rx_chunks = 100U;
    fake.cdc.callback_rx(0, nullptr);
    CHECK(fake.read_calls <= 8U);
    CHECK(fake.synthetic_rx_chunks >= 92U);
    std::array<std::uint8_t, 512U> data{};
    CHECK(transport.read({data.data(), data.size()}) == 256U);
    lineState(false);
    CHECK(transport.read({data.data(), data.size()}) == 0U);
    transport.resetSession();
    lineState(true);
    CHECK(transport.admitSession(transport.connectionEpoch()));
    CHECK(transport.read({data.data(), data.size()}) == 0U);
}
} // namespace

QueueHandle_t xQueueCreateStatic(UBaseType_t capacity, UBaseType_t item_size,
                                 std::uint8_t*, StaticQueue_t* queue) {
    queue->capacity = capacity;
    queue->item_size = item_size;
    return queue;
}
BaseType_t xQueueSend(QueueHandle_t queue, const void* item, TickType_t wait) {
    CHECK(wait == 0U);
    if (queue->items.size() >= queue->capacity) return pdFALSE;
    const auto* bytes = static_cast<const std::uint8_t*>(item);
    queue->items.emplace_back(bytes, bytes + queue->item_size);
    return pdTRUE;
}
BaseType_t xQueueReceive(QueueHandle_t queue, void* item, TickType_t wait) {
    CHECK(wait == 0U);
    if (queue->items.empty()) return pdFALSE;
    std::memcpy(item, queue->items.front().data(), queue->item_size);
    queue->items.pop_front();
    return pdTRUE;
}
esp_err_t tinyusb_driver_install(const tinyusb_config_t* config) { fake.device = *config; return ESP_OK; }
esp_err_t tinyusb_driver_uninstall() { return ESP_OK; }
esp_err_t tinyusb_cdcacm_init(const tinyusb_config_cdcacm_t* config) { fake.cdc = *config; return ESP_OK; }
bool tinyusb_cdcacm_initialized(tinyusb_cdcacm_itf_t) { return true; }
void tud_cdc_n_read_flush(std::uint8_t) { fake.rx.clear(); fake.synthetic_rx_chunks = 0U; }
bool tud_cdc_n_write_clear(std::uint8_t) { return true; }
esp_err_t tinyusb_cdcacm_read(tinyusb_cdcacm_itf_t, std::uint8_t* output,
                             std::size_t capacity, std::size_t* received) {
    ++fake.read_calls;
    if (fake.synthetic_rx_chunks != 0U) {
        --fake.synthetic_rx_chunks;
        std::fill_n(output, capacity, std::uint8_t{0x42U});
        *received = capacity;
    } else {
        *received = std::min(capacity, fake.rx.size());
        std::copy_n(fake.rx.data(), *received, output);
        fake.rx.erase(fake.rx.begin(), fake.rx.begin() + static_cast<std::ptrdiff_t>(*received));
    }
    return ESP_OK;
}
std::size_t tinyusb_cdcacm_write_queue(tinyusb_cdcacm_itf_t, const std::uint8_t* input,
                                      std::size_t size) {
    ++fake.write_calls;
    const auto count = std::min(fake.write_capacity, size);
    fake.wire.insert(fake.wire.end(), input, input + count);
    if (fake.detach_during_write) {
        fake.detach_during_write = false;
        deviceEvent(TINYUSB_EVENT_DETACHED);
    }
    return count;
}
esp_err_t tinyusb_cdcacm_write_flush(tinyusb_cdcacm_itf_t, std::uint32_t timeout) {
    CHECK(timeout == 0U);
    return ESP_OK;
}

int main() {
    Clock clock;
    usb::EspIdfUsbCdcTransport transport{clock};
    CHECK(transport.initialize());
    expiredPrefixPreservesNextFrame(transport, clock);
    expiredQueuedFramesReleaseCapacity(transport, clock);
    priorityDoesNotInterleaveFrames(transport, clock);
    reconnectDuringWriteDiscardsOldSession(transport, clock);
    receiveFloodHasBoundedCallbackWork(transport, clock);
    return failures == 0 ? 0 : 1;
}
