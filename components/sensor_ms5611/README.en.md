# MS5611 Sensor Domain

[فارسی](README.fa.md) | **English**

Keeps datasheet-defined MS5611 mathematics separate from I2C and scheduling so calibration and compensation can be verified with deterministic vectors.

## Main files

- `include/sensor_ms5611/ms5611_domain.hpp`: PROM calibration data, raw ADC values, CRC4 validation, and compensated result API.
- `src/ms5611_domain.cpp`: PROM validation and first/second-order pressure and temperature compensation.
- `test/ms5611_domain_test.cpp`: datasheet vectors, CRC failures, invalid PROM, low-temperature compensation, and numeric boundaries.

Actual I2C transactions are handled by the scheduler/platform layers, not by this domain component.
