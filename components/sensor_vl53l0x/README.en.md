# VL53L0X Four-Sensor Component

[فارسی](README.fa.md) | **English**

This component contains the platform-independent, allocation-free driver used for the four VL53L0X time-of-flight sensors. It is integrated with `SharedSensorScheduler`, but production composition keeps it disabled until the exact sensor and XSHUT wiring has been reviewed and recorded.

## Fixed role and instance contract

| Instance | Role | Runtime 7-bit address | Intended use |
|---:|---|---:|---|
| `0` | Ground/downward | `0x30` | Ground clearance; eligible input to the Android landing aid below approximately 2 m |
| `1` | Up | `0x31` | Obstacle distance above the aircraft; telemetry only at present |
| `2` | Front-left | `0x32` | Front-left obstacle distance; telemetry only at present |
| `3` | Front-right | `0x33` | Front-right obstacle distance; telemetry only at present |

These identities are protocol contracts, not discovery order. Changing them requires matching firmware, Android decoder, dashboard, and test changes.

Every VL53L0X starts at 7-bit address `0x29`. Four devices therefore require either four independently controlled active-low XSHUT lines or a separately implemented I2C multiplexer. The current implementation supports the XSHUT design. It first holds all four devices in shutdown, releases one device at a time, verifies model ID `0xEEAA`, assigns `0x30` through `0x33`, verifies the new address, and configures that device before proceeding. Wiring only SDA and SCL to all four modules is insufficient.

The project I2C bus is SDA GPIO8 and SCL GPIO9. The proposed, not physically authorized, XSHUT mapping is Ground GPIO12, Up GPIO13, Front-left GPIO14, and Front-right GPIO15. Those GPIOs must remain unique and must not overlap I2C, USB, actuator, memory, diagnostic, strapping-sensitive, or board-reserved pins.

## Fail-closed enablement

`CONFIG_SHAHBAZ_VL53L0X_ENABLE` defaults to `n`. Enabling the array also requires:

- `CONFIG_SHAHBAZ_SENSOR_HARDWARE_VERIFIED=y` and a nonempty `CONFIG_SHAHBAZ_SENSOR_EVIDENCE_RECORD_ID`;
- `CONFIG_SHAHBAZ_VL53L0X_XSHUT_PINS_PHYSICALLY_REVIEWED=y` and a nonempty `CONFIG_SHAHBAZ_VL53L0X_XSHUT_EVIDENCE_RECORD_ID`;
- four valid unique XSHUT GPIO assignments with no enabled-actuator conflict; and
- successful initialization of the fail-closed XSHUT GPIO adapter.

If any gate fails, all four rangefinders remain unavailable and no rangefinder sample is fabricated. The extended `DeviceStatusResponse` exposes each fixed role as disabled/unknown, initializing, live, or degraded; clients must still treat missing or stale instance telemetry as unavailable for control. The wire protocol does not export the internal `DriverError` counters.

## Scheduler behavior

The driver is cooperative: each `step()` performs at most one bounded I2C transfer, one bounded recovery action, or one XSHUT operation. It does not sleep, spin, or allocate. Each device has independent sequence, health, retry, and recovery state, while round-robin acquisition prevents one failed sensor from starving the others. A shared-bus recovery invalidates all I2C clients so every sensor is revalidated after the bus settles.

The default per-sensor cadence is 100,000 us. A Protocol v2 `SetSensorRate` request accepts sensor ID `3`, instances `0..3`, and an interval from 60,000 through 10,000,000 us. All four instances deliberately share the requested cadence; the instance identifies caller intent but does not create asymmetric bus scheduling.

## SensorSample contract

VL53L0X samples use sensor ID `3` and four unsigned 32-bit fields:

| Field ID | Meaning |
|---:|---|
| `5` | Distance in millimetres |
| `6` | Raw four-bit range status from `RESULT_RANGE_STATUS[6:3]` |
| `7` | Shahbaz control-eligibility quality: `100` when status and distance are accepted, otherwise `0` |
| `8` | Acquisition timing uncertainty in microseconds; `UINT32_MAX` means unknown/unrepresentable |

The timestamp is the midpoint between conversion-start command and result-read completion. Field 8 is the upward-rounded half-width of this interval. Delayed publication does not change either value; delayed reading broadens the uncertainty. The requested interval is start-to-start, with actual cadence limited by sequential acquisitions and shared bus load. See [timing and scheduling](../sensor_scheduler/README.en.md).

Field `7` is currently a conservative binary policy value, not a measured optical signal-strength percentage.

Raw range status `0` and `11` are accepted. The named decode also recognizes `1` sigma failure, `2` signal failure, `3` minimum-range failure, `4` phase failure, and `5` hardware failure; every other raw value is unknown. A distance is control-eligible only from 30 through 2000 mm inclusive and only with status `0` or `11`.

For every successfully read result, `TransportValid`, `CalibrationValid`, and `TimingValid` are set. `PlausibilityValid` is added only for a control-eligible result. `Fresh` is set on each new sample, and `RecoveredAfterError` marks the first successfully published sample after recovery. Health bit 0 means invalid range status; health bit 1 means distance outside the 30..2000 mm control range. The raw distance and status are still published for diagnosis, but consumers must not use a sample lacking full required validity for flight control.

The Ground sensor is only an observation source. The interface-board firmware does not decide touchdown or disarm. Android must combine fresh, validated Ground data with attitude/tilt compensation, continuity checks, estimator agreement, vertical speed, and an independent landed indication.

## Verification

The host tests cover domain parsing, every raw status, distance boundaries, SPAD selection, address validation, four-device address assignment, XSHUT failures, initialization and measurement timeouts, I2C recovery, retry backoff, publication backpressure, shared-bus invalidation, and round-robin fairness. They do not prove optical performance, sensor orientation, wiring integrity, I2C signal quality, field of view/occlusion, or landing safety on real hardware.

Primary references:

- [STMicroelectronics VL53L0X datasheet](https://www.st.com/resource/en/datasheet/vl53l0x.pdf)
- [ST application note AN4846: using multiple VL53L0X devices in one design](https://www.st.com/resource/en/application_note/an4846-using-multiple-vl53l0x-in-a-single-design-stmicroelectronics.pdf)
- [ST user manual UM2039](https://www.st.com/resource/en/user_manual/um2039-world-smallest-timeofflight-ranging-and-gesture-detection-sensor-application-programming-interface-stmicroelectronics.pdf)
- [PX4 rangefinder integration documentation](https://docs.px4.io/main/en/sensor/rangefinders)

The static tuning register sequence is adapted from the BSD-3-Clause PX4 VL53L0X driver; the attribution text is stored in `LICENSE-PX4-BSD-3-Clause.txt`.
