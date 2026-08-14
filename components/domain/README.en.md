# Domain Types

[فارسی](README.fa.md) | **English**

Defines neutral measurement types shared by sensors, scheduling, telemetry, and tests without coupling those layers to each other.

## Main files

- `include/shahbaz/domain/measurement.hpp`: typed measurements, timestamps, validity, quality, and health/status data.
- `test/measurement_test.cpp`: construction, stored values, and validity behavior.
- `CMakeLists.txt`: header-only C++17 component.

There is no ESP-IDF, FreeRTOS, USB, or hardware-specific code here.
