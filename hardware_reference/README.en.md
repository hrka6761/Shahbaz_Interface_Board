# Hardware Reference Library

[فارسی](README.fa.md) | **English**

This folder contains the hardware evidence and manufacturer references used by the Shahbaz Interface Board (`shahbaz_interface_board`) project. It is intentionally separate from `doc/`: `doc/` explains how to use the project, while `hardware_reference/` answers deeper engineering questions such as **which physical board/module is being used, which source supports a pin or protocol claim, and which facts are still unverified on the exact hardware in hand**.

## Read in this order

1. `01_esp32_s3_n16r8_development_board/` — ESP32-S3 N16R8 development-board references.
2. `02_sht30_i2c_module/` — the actual SHT3X-DIS-labeled I2C breakout used by the project plus official Sensirion chip documentation.
3. `03_gy63_ms5611_i2c_module/` — the actual GY-63 breakout used by the project plus official TE Connectivity MS5611 documentation.
4. `04_project_hardware_configuration/` — the wiring, GPIO policy, verification status, and measurements expected by this repository.
5. `05_project_hardware_diagrams/` — editable diagrams that visualize the configuration above.
6. `99_source_integrity/` — source catalog and hashes for locally stored official manufacturer files.

## Source policy

Manufacturer documentation is preferred. The current board description is best matched by the official VCC-GND Studio YD-ESP32-S3 design, so it is kept as the board-level **candidate reference** together with official Espressif and WCH documentation. The exact manufacturer/revision of the physical board remains unverified until front/back photos are matched.

The two sensor breakout boards are generic modules whose breakout-board manufacturers are not identifiable from the project photos. Therefore this repository does **not** pretend that a reseller page is an official module datasheet. For those modules:

- the project photo is the source of truth for visible breakout labels and exposed pins;
- Sensirion documentation is the source of truth for the SHT30/SHT3x chip protocol and limits;
- TE Connectivity documentation is the source of truth for the MS5611 chip protocol and limits;
- breakout-specific regulator, level-shifter, pull-up, and supply-range claims remain unverified unless measured or identified on the exact module.

Start with `../doc/English/14_HARDWARE_REFERENCE_AND_DATASHEETS.en.md` if you want the human-readable explanation before inspecting these raw references.
