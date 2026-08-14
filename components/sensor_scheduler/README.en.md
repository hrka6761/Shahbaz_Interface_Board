# Sensor Scheduler

[فارسی](README.fa.md) | **English**

Coordinates SHT30 and MS5611 work on the shared I2C bus without blocking the rest of the firmware. It owns sampling deadlines, bounded retry/recovery behavior, and publication of completed samples.

## Main files

- `include/shahbaz/sensors/sensor_scheduler.hpp`: scheduler states, deadlines, operations, and publication contract.
- `src/sensor_scheduler.cpp`: non-blocking SHT30/MS5611 sequences, rate handling, retry, and recovery.
- `test/sensor_scheduler_test.cpp`: fake-I2C/clock tests for timing, fairness, failures, recovery, and published measurements.

The scheduler depends on interfaces/domain and both sensor-domain components, while the actual ESP-IDF I2C implementation remains outside it.
