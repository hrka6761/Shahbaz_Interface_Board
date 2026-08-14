# ESP32-S3 N16R8 Development Board Reference

[فارسی](README.fa.md) | **English**

This directory documents the project development board. The best matching manufacturer reference currently available is **VCC-GND Studio YD-ESP32-S3** populated with an **ESP32-S3-WROOM-1-N16R8** module. Treat the VCC-GND board-level details as a candidate reference until the exact physical PCB is visually matched to the official front/schematic.

## What is here

- `01_official_vcc_gnd_board_docs/` — official VCC-GND Studio board repository, schematic link, official board images, and official WCH CH343 resources for the separate USB-to-UART bridge.
- `02_official_espressif_esp32_s3_docs/` — official Espressif ESP32-S3-WROOM-1 and ESP32-S3 SoC datasheets plus official native-USB/USB-device documentation.
- `03_actual_project_board_photos/` — front/back photos of the exact board in hand; these are the evidence required before treating the YD board revision as physically verified.

## Project-relevant facts

- The official YD-ESP32-S3 design has two Type-C connectors: one directly connected to ESP32-S3 native USB, and another through a WCH CH343P USB-to-UART bridge.
- Native ESP32-S3 USB uses GPIO19 for D- and GPIO20 for D+.
- The official YD board maps the onboard WS2812 RGB LED to GPIO48 and UART0 to GPIO43/GPIO44.
- For 8-line flash/PSRAM module variants, the YD documentation warns that GPIO35, GPIO36, and GPIO37 are used internally and are not available externally.
- Espressif defines ESP32-S3-WROOM-1-N16R8 as 16 MB Quad SPI flash plus 8 MB Octal SPI PSRAM.

For Shahbaz USB telemetry, use the **native ESP32-S3 USB Type-C port**, not the CH343P USB-to-UART port.
