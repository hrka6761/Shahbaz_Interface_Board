# Domain Types

[فارسی](README.fa.md) | **English**

Defines neutral measurement types shared by sensors, scheduling, telemetry, and tests without coupling those layers to each other.

## Main files

- `include/shahbaz/domain/measurement.hpp`: typed measurements, timestamps, validity, quality, and health/status data.
- `test/measurement_test.cpp`: construction, stored values, and validity behavior.
- `CMakeLists.txt`: header-only C++17 component.

There is no ESP-IDF, FreeRTOS, USB, or hardware-specific code here.

`AcquisitionWindow` brackets conversion-start/result-read times, computes an overflow-safe midpoint, and rounds its maximum timing error upward. SensorSample field `8` carries that error as unsigned microseconds; `UINT32_MAX` is reserved for unbounded/unrepresentable uncertainty. SHT30/MS5611 publish three fields and VL53L0X four, within the unchanged four-field capacity. See the [timing contract](../sensor_scheduler/README.en.md).
