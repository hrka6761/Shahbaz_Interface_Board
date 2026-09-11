# Sensor Scheduler

[فارسی](README.fa.md) | **English**

Coordinates SHT30, MS5611, and the optional four-device VL53L0X array on the shared I2C bus. It owns sampling deadlines, bounded retry/recovery behavior, and publication of completed samples.

## Main files

- `include/shahbaz/sensors/sensor_scheduler.hpp`: scheduler states, deadlines, operations, and publication contract.
- `src/sensor_scheduler.cpp`: non-blocking SHT30/MS5611 sequences, rate handling, retry, and recovery.
- `test/sensor_scheduler_test.cpp`: fake-I2C/clock tests for timing, fairness, failures, recovery, and published measurements.

The scheduler depends on interfaces/domain and the three sensor-domain components, while the actual ESP-IDF I2C implementation remains outside it.

## Acquisition time, uncertainty, and cadence

Each producer captures the board's monotonic clock immediately before the conversion-start command and after the successful result read. `AcquisitionWindow` computes their overflow-safe midpoint for `SensorSample.monotonic_timestamp_us` and the upward-rounded half-width for unsigned field `8`, `AcquisitionTimeUncertaintyMicros`. A delayed I2C read therefore increases observable timing uncertainty. Enqueueing, interrupt clearing, USB transport, or Android arrival must never regenerate the observation timestamp. Field `8 = UINT32_MAX` means unbounded/unrepresentable uncertainty.

For MS5611 the sample timestamp and field 8 describe the pressure D1 conversion. Internal temperature is compensated using the latest validated D2; it is not a simultaneous independent temperature observation. D2 freshness is checked conservatively from its conversion start through D1 result-read completion (default maximum age 400 ms). PROM CRC validates calibration data, not a nonexistent CRC on each ADC read. Pressure plausibility and calibration validity do not prove low pressure noise or a calibrated altitude. QNH, filtering, covariance, and flight decisions belong to Android.

| Producer | Default requested interval | Acquisition behavior |
|---|---:|---|
| SHT30 | 500 ms | High-repeatability conversion, datasheet maximum wait plus 2 ms margin; result CRC checked |
| MS5611 pressure D1 | 40 ms | OSR4096, maximum conversion wait plus 200 us margin; phase-anchored requested cadence |
| MS5611 temperature D2 | 250 ms | OSR4096; cached for compensation only within the age limit |
| VL53L0X per role | 100 ms | Sequential single-shot acquisitions, 60 ms measurement timeout; interval measured start-to-start |

Requested intervals are scheduling targets, not guaranteed delivered sample rates. Four rangefinders share one sequential driver and compete fairly with SHT30/MS5611 on one bus. Conversion time, I2C transactions, retry/recovery, USB service, and RTOS scheduling all affect actual cadence; consumers must use each sample's sequence and acquisition time rather than assume a fixed frequency.

`step()` still advances at most one sensor state and performs at most one bounded bus operation. `service_ready(4)` in `app_main` additionally drains immediately-ready state-only transitions, stopping at the first I2C/recovery operation or the fixed transition budget. Round-robin selection and electrical recovery barriers remain enforced. USB RX/TX, command dispatch, safety supervision and watchdog service run between slices. The main loop sleeps one tick (`CONFIG_FREERTOS_HZ=1000` by default); this is not a certified real-time sampling guarantee.

The existing protocol v2 field tuples carry the new timing field without changing the 22-byte frame header or CRC/COBS layout. Updated Android combines producer uncertainty with clock-sync uncertainty and transport age. Old captures without field 8 remain diagnostic input but have unknown acquisition timing and cannot authorize precision control. Old strict-schema Android builds can reject extended samples; deploy matched app/firmware versions. See the [wire contract](../../doc/English/12_USB_PROTOCOL.en.md).

Host tests exercise delayed result reads, midpoint and half-width arithmetic including overflow, publication delay, requested cadence, bounded scheduler slices, fairness, stale compensation, and recovery barriers. Hardware timing/noise characterization, vibration/propwash tests, HIL and flight acceptance remain separate evidence.
