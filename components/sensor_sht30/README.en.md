# SHT30 Sensor Domain

[فارسی](README.fa.md) | **English**

Keeps SHT30 response validation and datasheet conversion mathematics separate from I2C and scheduling.

## Main files

- `include/sensor_sht30/sht3x_domain.hpp`: raw response parsing, CRC8, and temperature/humidity result API.
- `src/sht3x_domain.cpp`: Sensirion CRC checks and conversion to physical units.
- `test/sht3x_domain_test.cpp`: known vectors, independent CRC words, corrupt frames, and conversion boundaries.

Actual I2C transactions are handled by the scheduler/platform layers.
