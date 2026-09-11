#pragma once
#include <cstdint>
using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
constexpr esp_err_t ESP_FAIL = -1;
enum { TINYUSB_EVENT_ATTACHED, TINYUSB_EVENT_DETACHED };
struct tinyusb_event_t { int id{}; };
using tinyusb_event_cb_t = void (*)(tinyusb_event_t*, void*);
struct tinyusb_config_t { tinyusb_event_cb_t callback{}; void* argument{}; };
esp_err_t tinyusb_driver_install(const tinyusb_config_t* config);
esp_err_t tinyusb_driver_uninstall();
void tud_cdc_n_read_flush(std::uint8_t interface_number);
bool tud_cdc_n_write_clear(std::uint8_t interface_number);
