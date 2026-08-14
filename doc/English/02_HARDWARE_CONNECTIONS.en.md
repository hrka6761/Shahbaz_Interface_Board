# 02 — Connecting the Board and the Two I2C Modules

[فارسی](../فارسی/02_HARDWARE_CONNECTIONS.fa.md) | **English**

> Do not connect motors, ESCs, servos, or propellers during this stage.

This project uses these physical modules:

- VCC-GND Studio YD-ESP32-S3-compatible N16R8 development board reference.
- Four-pin SHT3X-DIS-labeled SHT30 I2C breakout.
- GY-63 breakout expected to contain MS5611-01BA03, used in I2C mode.

The two breakout manufacturers are not identified, so the wiring below uses the visible labels on the actual project modules and the official Sensirion/TE chip protocols rather than reseller assumptions.

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
```

### Why the three extra GY-63 pins matter

The official MS5611 datasheet defines `PS=HIGH` as I2C mode. In I2C mode, `CSB` must be tied either high or low and must not float. Shahbaz uses `CSB=LOW`, which corresponds to address `0x77`. `SDO` is an SPI output and is not used by the project in I2C mode.

## 2. Important checks before applying power

1. Recheck VCC and GND on both actual modules.
2. Make sure SDA and SCL are not swapped.
3. Power both project modules from 3.3 V for this project configuration.
4. Do not apply a 5 V logic level directly to ESP32-S3 GPIO pins.
5. Keep a common ground between the ESP32 board and both modules.
6. Do not assume a reseller's claimed VCC range, onboard regulator, level shifter, or pull-up values apply to your generic breakout. Those breakout-level details are treated as unverified until measured or identified on the actual module.

## 3. Which USB Type-C connector to use

The official YD-ESP32-S3 design has **two different Type-C connectors**:

- **Native ESP32-S3 USB** — directly connected to ESP32-S3 USB; Shahbaz telemetry uses this connector. GPIO19 is D- and GPIO20 is D+.
- **USB-to-UART** — connected through the board's WCH CH343P bridge; use it for programming/serial diagnostics when appropriate, not as the Shahbaz native USB data link.

Before relying on board-level connector placement, visually match your physical board to the official YD-ESP32-S3 reference stored under `hardware_reference/01_esp32_s3_n16r8_development_board/`.

## 4. Quick checklist before connecting USB

- [ ] SHT3X-DIS module: VCC -> 3.3 V, GND -> GND, SDA -> GPIO8, SCL -> GPIO9.
- [ ] GY-63 module: VCC -> 3.3 V, GND -> GND, SDA -> GPIO8, SCL -> GPIO9.
- [ ] GY-63: PS -> 3.3 V, CSB -> GND, SDO -> NC.
- [ ] There is no short circuit between 3.3 V and GND.
- [ ] Actuators are disabled or physically disconnected.
- [ ] The USB cable supports data transfer.
- [ ] The cable is connected to the native ESP32-S3 USB Type-C connector, not the CH343P USB-to-UART connector.

For source files, module photos, pin restrictions, and verification status, see `14_HARDWARE_REFERENCE_AND_DATASHEETS.en.md` and `../../hardware_reference/README.en.md`.

**Next:** `03_DEVELOPMENT_TOOLS_SETUP.en.md`.
