#pragma once
#include "tinyusb.h"
#include <cstddef>
enum tinyusb_cdcacm_itf_t { TINYUSB_CDC_ACM_0 };
enum { CDC_EVENT_LINE_STATE_CHANGED };
struct cdcacm_event_t { int type{}; struct { bool dtr{}; } line_state_changed_data; };
using cdcacm_callback_t = void (*)(int, cdcacm_event_t*);
struct tinyusb_config_cdcacm_t {
    tinyusb_cdcacm_itf_t cdc_port{};
    cdcacm_callback_t callback_rx{};
    cdcacm_callback_t callback_rx_wanted_char{};
    cdcacm_callback_t callback_line_state_changed{};
    cdcacm_callback_t callback_line_coding_changed{};
};
esp_err_t tinyusb_cdcacm_init(const tinyusb_config_cdcacm_t* config);
bool tinyusb_cdcacm_initialized(tinyusb_cdcacm_itf_t interface_number);
esp_err_t tinyusb_cdcacm_read(tinyusb_cdcacm_itf_t interface_number,
                             std::uint8_t* output, std::size_t capacity,
                             std::size_t* received);
std::size_t tinyusb_cdcacm_write_queue(tinyusb_cdcacm_itf_t interface_number,
                                      const std::uint8_t* input, std::size_t size);
esp_err_t tinyusb_cdcacm_write_flush(tinyusb_cdcacm_itf_t interface_number,
                                    std::uint32_t timeout);
