# ESP-IDF Platform Adapters

[فارسی](README.fa.md) | **English**

Connects portable firmware interfaces to ESP-IDF services on the real ESP32-S3 target.

## Main adapters

- `espidf_monotonic_clock`: microsecond monotonic clock backed by `esp_timer`.
- `espidf_board_validator`: runtime flash/PSRAM validation plus project GPIO/evidence gates, including actuator-pin/evidence faults.
- `espidf_i2c_bus`: real ESP-IDF I2C master with bounded operations and bus recovery. **Both initialization and recovery enforce the board pin policy internally**, so a caller cannot recover/toggle a reserved USB, strapping, nonexistent, or memory-path GPIO by bypassing `app_main` validation.

`app_main` also skips I2C initialization when board validation reports an invalid I2C assignment. Native USB CDC remains isolated in `usb_transport_espidf`; PWM output remains isolated in `actuator_espidf`.
