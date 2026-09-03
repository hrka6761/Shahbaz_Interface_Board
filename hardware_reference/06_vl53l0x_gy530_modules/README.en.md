# Four GY-530 / VL53L0X Rangefinder Modules

[فارسی](README.fa.md) | **English**

The project owner identifies four installed breakout boards as GY-530 modules containing VL53L0X sensors. No exact-board front/back photographs, manufacturer, schematic, or measured pin/electrical record is currently stored here. Consequently, STMicroelectronics documentation is authoritative for the VL53L0X chip, while the breakout's regulator, level shifting, pull-ups, pin order, and acceptable VCC range remain unverified.

## Required project wiring

All four modules share the 3.3 V-compatible I2C bus:

```text
all SDA -> ESP32-S3 GPIO8
all SCL -> ESP32-S3 GPIO9
all GND -> common ground
```

Verify the exact module's supply input before connecting VCC. Do not infer that a generic GY-530 advertisement applies to these physical boards, and never expose an ESP32-S3 GPIO to 5 V logic.

Each module must also expose its VL53L0X active-low XSHUT signal and connect it to a different reviewed ESP32-S3 GPIO:

| Fixed instance | Mounting role | Proposed XSHUT GPIO | Runtime address |
|---:|---|---:|---:|
| `0` | Ground/downward | GPIO12 | `0x30` |
| `1` | Upward | GPIO13 | `0x31` |
| `2` | Front-left | GPIO14 | `0x32` |
| `3` | Front-right | GPIO15 | `0x33` |

GPIO12..15 are a firmware proposal, not evidence that those routes are safe or already connected on the aircraft. Record continuity, polarity, shorts, conflicts, and the exact physical sensor-to-role mapping before enabling the feature. The firmware polls for completion, so a breakout interrupt/GPIO1 output is not required by the present implementation.

## Why four XSHUT lines are mandatory here

All four VL53L0X devices power up at 7-bit address `0x29`. Parallel devices at that address cannot be individually addressed. Firmware therefore asserts every XSHUT low and releases one module at a time to give it a unique volatile address. Those assigned addresses are lost at reset and are recreated at every boot.

An I2C multiplexer is a valid alternative hardware architecture in general, but this repository does not currently implement one. Do not enable the existing array driver for a mux design without implementing and testing a matching adapter.

## Mounting and flight-use limitations

- The downward unit must have an unobstructed optical path past the frame and landing gear. Its reported distance is along its optical axis; Android performs tilt projection and other validity checks before using it near the ground.
- The upward and two forward units are currently telemetry sources only. Firmware does not perform obstacle avoidance from them.
- The project accepts only 30..2000 mm as control-eligible. This is a software policy, not a promise that every surface, light condition, cover, or mounting geometry will range reliably at 2 m.
- The four sensors are sampled cooperatively rather than ranging concurrently, but optical crosstalk, ambient light, target reflectance, field of view, vibration, contamination, and cover-window effects still require aircraft-level tests.
- A range sample alone must never trigger touchdown or disarm. The Android landing aid must require independent landing evidence.

## Evidence still required

- Clear front/back photo and readable labels for each exact GY-530 board.
- Confirmed VCC, GND, SDA, SCL, XSHUT, and any GPIO1 pin mapping for that board revision.
- Measured idle logic voltage, effective pull-up resistance, rise time, and bus operation with all six I2C modules attached.
- Continuity record mapping each XSHUT line to GPIO12/13/14/15 and to the intended physical role.
- Boot/reboot evidence that address assignment produces exactly `0x30`, `0x31`, `0x32`, and `0x33` with no device left at `0x29`.
- Static-target, surface, sunlight, tilt, vibration, occlusion, crosstalk, stale-data, disconnect, and single-sensor fault testing before any propulsion test.

Until that evidence exists, keep `CONFIG_SHAHBAZ_VL53L0X_ENABLE=n`.

## Primary references

- [STMicroelectronics VL53L0X datasheet](https://www.st.com/resource/en/datasheet/vl53l0x.pdf)
- [ST AN4846: using multiple VL53L0X devices in one design](https://www.st.com/resource/en/application_note/an4846-using-multiple-vl53l0x-in-a-single-design-stmicroelectronics.pdf)
- [ST UM2039 VL53L0X API user manual](https://www.st.com/resource/en/user_manual/um2039-world-smallest-timeofflight-ranging-and-gesture-detection-sensor-application-programming-interface-stmicroelectronics.pdf)
- [PX4 rangefinder integration documentation](https://docs.px4.io/main/en/sensor/rangefinders)

These references describe the sensor chip and integration principles. They do not identify or certify a generic GY-530 breakout.
