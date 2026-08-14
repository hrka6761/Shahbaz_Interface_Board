# 14 — Hardware Reference, Modules, Pins, and Manufacturer Sources

[فارسی](../فارسی/14_HARDWARE_REFERENCE_AND_DATASHEETS.fa.md) | **English**

This is the final document in the recommended reading sequence. The raw engineering references are stored under `hardware_reference/`, organized by the **actual project board/module first**, then by source authority.

## 1. Hardware used by this project

| Project hardware | Current identity | Manufacturer-reference policy |
|---|---|---|
| ESP32-S3 development board | YD-ESP32-S3-compatible board with ESP32-S3-WROOM-1-N16R8 target | VCC-GND Studio board docs + Espressif module/SoC docs + WCH CH343 docs |
| Temperature/humidity module | Generic four-pin breakout labeled SHT3X-DIS, expected SHT30 | Actual project module photo + official Sensirion SHT3x/SHT30 docs |
| Pressure module | Generic GY-63 breakout, expected MS5611-01BA03 | Actual project module photo + official TE Connectivity MS5611 docs |

The breakout-board manufacturers for the two sensor modules are not identifiable from the supplied photos. The project therefore avoids reseller documentation as a source of truth for breakout-specific electrical claims.

## 2. Project hardware profile

```text
Board reference: VCC-GND Studio YD-ESP32-S3
ESP32 module: ESP32-S3-WROOM-1-N16R8
Flash: 16 MB Quad SPI
PSRAM: 8 MB Octal SPI
I2C: SDA GPIO8, SCL GPIO9, target 400 kHz
Native USB: GPIO19 D-, GPIO20 D+
SHT30 configured address: 0x44
MS5611 configured address: 0x77
GY-63 straps: PS HIGH, CSB LOW, SDO NC
```

Espressif defines N16R8 as 16 MB flash plus 8 MB Octal SPI PSRAM. The official VCC-GND YD-ESP32-S3 reference documents a native USB Type-C connector directly using GPIO19/20, a separate CH343P USB-to-UART Type-C connector, an onboard WS2812 on GPIO48, and UART0 on GPIO43/GPIO44.

## 3. Important GPIO policy

| GPIO | Project status |
|---:|---|
| 0, 3, 45, 46 | Strapping-sensitive; review before use |
| 4, 5, 6, 7 | Actuator PWM only when actuator output is deliberately enabled and reviewed |
| 8 | Project I2C SDA |
| 9 | Project I2C SCL |
| 10, 11 | Servo PWM only when actuator output is deliberately enabled and reviewed |
| 19 | Native USB D-; reserved |
| 20 | Native USB D+; reserved |
| 22..25 | Nonexistent ESP32-S3 GPIO numbers |
| 26..34 | Internal flash/PSRAM memory path; do not assign |
| 35..37 | Unavailable with the N16R8 8-line PSRAM configuration |
| 38 | Available only after normal project review; the official YD header exposes it |
| 43, 44 | UART0 / diagnostic path; reserved |
| 48 | Official YD onboard WS2812 RGB LED; reserved |

## 4. Folder layout and where to look

```text
hardware_reference/
  01_esp32_s3_n16r8_development_board/
    01_official_vcc_gnd_board_docs/
    02_official_espressif_esp32_s3_docs/
  02_sht30_i2c_module/
    01_official_sensirion_chip_docs/
    02_actual_project_module_photos/
  03_gy63_ms5611_i2c_module/
    01_official_te_connectivity_chip_docs/
    02_actual_project_module_photos/
  04_project_hardware_configuration/
  05_project_hardware_diagrams/
  99_source_integrity/
```

Each top-level hardware folder has its own bilingual README explaining what is authoritative and what remains unverified.

## 5. Official sources stored or linked locally

### Development board / ESP32

- VCC-GND Studio YD-ESP32-S3 official repository and V1.4 schematic link.
- Official VCC-GND board-front and hardware-overview images.
- Espressif ESP32-S3-WROOM-1/WROOM-1U datasheet.
- Espressif ESP32-S3 SoC datasheet.
- Espressif stable native-USB/USB-device documentation.
- WCH CH343 datasheet page and Windows driver page for the board's separate USB-to-UART bridge.

### SHT30 module

- Sensirion SHT3x-DIS datasheet.
- Sensirion SHT30-DIS-B official product page.
- Sensirion official humidity-sensor handling instructions.
- Sensirion official ambient-condition testing guidance.
- Actual project SHT3X-DIS breakout photo.

### GY-63 / MS5611 module

- TE Connectivity MS5611-01BA03 official datasheet.
- TE Connectivity MS5611 official product page.
- Actual project GY-63 breakout photo.

## 6. Why the project does not use a generic GY-63/SHT30 reseller datasheet

`GY-63` and the four-pin `SHT3X-DIS` breakout in this project are generic module designs. Without a verified breakout manufacturer, a reseller page cannot reliably prove the exact regulator, level shifter, pull-up network, VCC range, or PCB revision on the module you physically have.

For that reason, this project distinguishes:

- **chip-level facts** — taken from Sensirion, TE Connectivity, Espressif, or WCH;
- **board-level YD facts** — taken from VCC-GND Studio;
- **visible breakout facts** — taken from the actual project module photo;
- **measured facts** — stored only after physical measurement;
- **project configuration** — what the firmware/wiring deliberately chooses.

## 7. Physical verification still required

Before treating all board-level assumptions as fully verified, record:

- clear front/back photos of the exact ESP32 development board and readable module marking;
- confirmation that the physical board matches the VCC-GND YD-ESP32-S3 reference/revision;
- I2C scan showing SHT30 at `0x44` and MS5611 at `0x77`;
- confirmation of the GY-63 PS/CSB wiring used by the assembled module;
- measured I2C idle voltage and effective pull-up resistance if bus integrity becomes a concern;
- USB enumeration on the native USB Type-C connector;
- any power/backfeed measurements required by the intended final drone power arrangement.

## 8. Integrity and validation

`hardware_reference/99_source_integrity/source_catalog.csv` records the authority and purpose of every reference. `official_files_sha256.txt` hashes official manufacturer PDFs/images stored locally.

Run the repository validator with:

```powershell
py -3 tools\validate_graphics_inputs.py
```

An `UNVERIFIED` result is not a documentation failure; it means the repository deliberately refuses to turn an unmeasured physical fact into a claim.

This is the last document in the sequence. To use the project from the beginning, return to `00_START_HERE.en.md`.
