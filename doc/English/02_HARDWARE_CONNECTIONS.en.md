# 02 — Connecting the Board and Six I2C Sensor Modules

[فارسی](../فارسی/02_HARDWARE_CONNECTIONS.fa.md) | **English**

> Do not connect motors, ESCs, servos, or propellers during this stage.

This project uses these physical modules:

- VCC-GND Studio YD-ESP32-S3-compatible N16R8 development board reference.
- Four-pin SHT3X-DIS-labeled SHT30 I2C breakout.
- GY-63 breakout expected to contain MS5611-01BA03, used in I2C mode.
- Four GY-530 breakouts identified by the project owner as VL53L0X modules: Ground, Up, Front-left, and Front-right.

The breakout manufacturers are not identified. The SHT30/MS5611 wiring uses visible labels from the available project photos and official Sensirion/TE chip protocols. No exact GY-530 photo or schematic is stored yet, so verify each module's labels and electrical design against ST's VL53L0X documentation instead of relying on a reseller page.

## 1. Shared I2C connection

| Function | ESP32-S3 board | SHT3X-DIS module | GY-63 / MS5611 module |
|---|---|---|---|
| 3.3 V power | 3V3 | VCC | VCC |
| Ground | GND | GND | GND |
| I2C data | GPIO8 | SDA | SDA |
| I2C clock | GPIO9 | SCL | SCL |
| Interface select | — | — | PS -> 3.3 V / HIGH |
| Address select | — | — | CSB -> GND / LOW |
| SPI data output | — | — | SDO -> NC |

The current firmware configuration is:

```text
SDA = GPIO8
SCL = GPIO9
SHT30 address = 0x44
MS5611 address = 0x77
I2C target frequency = 400 kHz
VL53L0X power-up address = 0x29 (all four devices)
VL53L0X runtime addresses = 0x30, 0x31, 0x32, 0x33
VL53L0X feature default = disabled
```

### Why the three extra GY-63 pins matter

The official MS5611 datasheet defines `PS=HIGH` as I2C mode. In I2C mode, `CSB` must be tied either high or low and must not float. Shahbaz uses `CSB=LOW`, which corresponds to address `0x77`. `SDO` is an SPI output and is not used by the project in I2C mode.

## 2. Four VL53L0X connections

All four VL53L0X modules share SDA GPIO8, SCL GPIO9, and common ground. Verify the exact GY-530 breakout's VCC input and logic levels before applying power; only 3.3 V-compatible logic may reach the ESP32-S3.

SDA/SCL alone is not enough. Every VL53L0X starts at the same 7-bit address `0x29`, so the implemented design requires one independently controlled active-low XSHUT signal per module:

| Fixed instance | Physical role | Shared bus | Proposed XSHUT | Assigned runtime address |
|---:|---|---|---:|---:|
| `0` | Ground/downward | SDA GPIO8 / SCL GPIO9 | GPIO12 | `0x30` |
| `1` | Upward | SDA GPIO8 / SCL GPIO9 | GPIO13 | `0x31` |
| `2` | Front-left | SDA GPIO8 / SCL GPIO9 | GPIO14 | `0x32` |
| `3` | Front-right | SDA GPIO8 / SCL GPIO9 | GPIO15 | `0x33` |

GPIO12..15 are proposed defaults, not proof of physical wiring. Before enabling the feature, record the exact module pinout, continuity from every XSHUT pin to its GPIO, sensor orientation/role, absence of shorts, and absence of conflicts with actuators or other board functions. The present firmware polls measurement completion; a module GPIO1/interrupt output is not used.

The firmware holds all modules in XSHUT, then releases, identifies, readdresses, verifies, and configures one module at a time. Assigned addresses are volatile and are recreated after every reset. An I2C multiplexer is not implemented in this repository.

The rangefinder gate deliberately defaults closed. Do not set `CONFIG_SHAHBAZ_VL53L0X_ENABLE=y` until both the common sensor-hardware evidence and the dedicated four-XSHUT evidence are real, recorded, and selected in Kconfig. A missing/invalid/overlapping XSHUT assignment blocks all four devices.

## 3. Important checks before applying power

1. Recheck VCC and GND on all actual modules.
2. Make sure SDA and SCL are not swapped.
3. Power SHT30 and GY-63 from 3.3 V for this project configuration; verify the exact GY-530 VCC input before connecting it.
4. Do not apply a 5 V logic level directly to ESP32-S3 GPIO pins.
5. Keep a common ground between the ESP32 board and every module.
6. Do not assume a reseller's claimed VCC range, onboard regulator, level shifter, or pull-up values apply to your generic breakout. Those breakout-level details are treated as unverified until measured or identified on the actual module.
7. Check the effective SDA/SCL pull-up resistance and rise time with all six modules connected; parallel breakout pull-ups may make the bus load too strong.

## 4. Which USB Type-C connector to use

The official YD-ESP32-S3 design has **two different Type-C connectors**:

- **Native ESP32-S3 USB** — directly connected to ESP32-S3 USB; Shahbaz telemetry uses this connector. GPIO19 is D- and GPIO20 is D+.
- **USB-to-UART** — connected through the board's WCH CH343P bridge; use it for programming/serial diagnostics when appropriate, not as the Shahbaz native USB data link.

Before relying on board-level connector placement, visually match your physical board to the official YD-ESP32-S3 reference stored under `hardware_reference/01_esp32_s3_n16r8_development_board/`.

## 5. Quick checklist before connecting USB

- [ ] SHT3X-DIS module: VCC -> 3.3 V, GND -> GND, SDA -> GPIO8, SCL -> GPIO9.
- [ ] GY-63 module: VCC -> 3.3 V, GND -> GND, SDA -> GPIO8, SCL -> GPIO9.
- [ ] GY-63: PS -> 3.3 V, CSB -> GND, SDO -> NC.
- [ ] Each VL53L0X module: verified VCC, common GND, SDA -> GPIO8, SCL -> GPIO9.
- [ ] Ground/Up/Front-left/Front-right XSHUT continuity has been recorded for proposed GPIO12/13/14/15 respectively.
- [ ] All four physical roles and optical-axis directions are labelled and match instances `0/1/2/3`.
- [ ] The common sensor-evidence ID and dedicated XSHUT-evidence ID identify real records; rangefinders otherwise remain disabled.
- [ ] There is no short circuit between 3.3 V and GND.
- [ ] Actuators are disabled or physically disconnected.
- [ ] The USB cable supports data transfer.
- [ ] The cable is connected to the native ESP32-S3 USB Type-C connector, not the CH343P USB-to-UART connector.

For source files, module photos, pin restrictions, and verification status, see `14_HARDWARE_REFERENCE_AND_DATASHEETS.en.md`, `../../hardware_reference/README.en.md`, and `../../hardware_reference/06_vl53l0x_gy530_modules/README.en.md`.

**Next:** `03_DEVELOPMENT_TOOLS_SETUP.en.md`.
