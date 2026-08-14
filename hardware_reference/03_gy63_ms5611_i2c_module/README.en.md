# GY-63 MS5611 I2C Module Reference

[فارسی](README.fa.md) | **English**

The project uses a **generic GY-63 breakout** expected to contain an MS5611-01BA03. The breakout-board manufacturer is not identifiable from the project photo, so board-level claims are kept separate from official TE Connectivity MS5611 documentation.

## Contents

- `01_official_te_connectivity_chip_docs/` — official TE Connectivity MS5611 product page and datasheet.
- `02_actual_project_module_photos/` — the actual GY-63 module image supplied for this project.

## I2C configuration used by Shahbaz

The project photo shows exposed `VCC`, `GND`, `SCL`, `SDA`, `CSB`, `SDO`, and `PS` pins. The project configuration is:

```text
VCC -> 3.3 V
GND -> GND
SCL -> GPIO9
SDA -> GPIO8
PS  -> 3.3 V (HIGH, selects I2C)
CSB -> GND   (LOW, selects I2C address 0x77)
SDO -> NC    (unused in I2C mode)
```

TE Connectivity specifies that PS high selects I2C. In I2C mode CSB must not float; it selects the address bit. Shahbaz uses CSB low and address `0x77`.

Do not assume the GY-63 breakout itself is 5 V safe merely because some reseller listings say so. Its breakout regulator, pull-ups, and level shifting are not yet manufacturer-verified for the exact module in this project; 3.3 V is the project supply choice.
