# SHT30 I2C Module Reference

[فارسی](README.fa.md) | **English**

The project uses a **generic four-pin breakout labeled SHT3X-DIS**. The breakout-board manufacturer is unknown, so this directory deliberately separates the actual module photo from official Sensirion chip documentation.

## Contents

- `01_official_sensirion_chip_docs/` — official Sensirion SHT3x-DIS datasheet, SHT30 product page, handling guidance, and ambient-test guidance.
- `02_actual_project_module_photos/` — the actual module image supplied for this project.

## What is verified for this project

The visible breakout labels are `VCC`, `GND`, `SDA`, and `SCL`. Firmware is configured for I2C address `0x44`. Sensirion documents `0x44` as the default SHT3x-DIS address when ADDR is low, and `0x45` when ADDR is high.

Because the breakout maker is unknown, do not infer its regulator, level shifting, pull-up values, or external VCC range from unrelated reseller listings. The project powers this breakout from the ESP32 board's 3.3 V rail and verifies communication on the assembled hardware.
