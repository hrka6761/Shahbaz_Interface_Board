# 00 — Start Here

[فارسی](../فارسی/00_START_HERE.fa.md) | **English**

This page is the reading map for the **Shahbaz Interface Board** project. The operational product path is the ESP32-S3 board connected by native USB to an **Android phone running the Shahbaz application**. Windows is a development and HIL-validation host only; it is not the operational destination for sensor data.

## Product data path

```text
SHT30 + MS5611
      |
      | I2C
      v
shahbaz_interface_board / ESP32-S3
      |
      | Native USB CDC — Shahbaz Protocol v2
      v
Android phone — USB Host
      |
      v
Shahbaz Android application
      |
      +-> live temperature / humidity / pressure
      +-> barometric altitude = pressure + app QNH
```

The board transmits raw validated sensor telemetry over native USB. The Shahbaz Android application is the operational client. It supplies QNH and calculates barometric altitude from MS5611 pressure. The Windows HIL tools exercise the same protocol only to isolate firmware/board faults during development.

## Recommended reading order

| Step | Document | Purpose |
|---|---|---|
| 1 | `01_PROJECT_OVERVIEW.en.md` | Understand product roles and system boundaries. |
| 2 | `02_HARDWARE_CONNECTIONS.en.md` | Wire the board and sensors safely. |
| 3 | `03_DEVELOPMENT_TOOLS_SETUP.en.md` | Prepare ESP-IDF and development/HIL tools. |
| 4 | `04_BUILD_FLASH_AND_USB.en.md` | Build/flash firmware and understand the two USB roles. |
| 5 | `05_CODE_TESTING.en.md` | Run hardware-independent tests and validators. |
| 6 | `06_WINDOWS_BOARD_HIL_TEST.en.md` | Validate the board/firmware independently on Windows. |
| 7 | `07_ANDROID_SHAHBAZ_INTEGRATION_TEST.en.md` | Validate the real Android + Shahbaz operational path. |
| 8 | `08_COMPLETE_SYSTEM_ACCEPTANCE.en.md` | Decide whether the current product stage is accepted. |
| 9 | `09_TROUBLESHOOTING.en.md` | Isolate Android, USB, protocol, sensor, and board faults. |

Documents 10–14 cover safety, architecture, protocol, extension rules, and hardware evidence.

## One rule to keep the architecture clear

**Android + Shahbaz App is the operational host. Windows HIL is a diagnostic test host.** A Windows HIL PASS does not by itself mean the product has passed end-to-end acceptance.

**Next:** `01_PROJECT_OVERVIEW.en.md`.
