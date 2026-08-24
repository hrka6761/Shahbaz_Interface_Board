# 04 — Build, Flash, and USB Roles

[فارسی](../فارسی/04_BUILD_FLASH_AND_USB.fa.md) | **English**

## 1. Safe default configuration

Keep physical actuators disabled for sensor/USB integration:

```text
CONFIG_SHAHBAZ_ACTUATORS_ENABLE=n
```

For interactive USB sessions, `sdkconfig.defaults` selects the reviewed `1000 ms` heartbeat timeout for the Android and Windows-HIL 350 ms heartbeat cadence. The reusable component's Kconfig fallback remains intentionally fail-closed at `0 ms` when that project configuration is absent.

Keep the primary diagnostic console on UART0 and set `CONFIG_ESP_CONSOLE_SECONDARY_NONE=y`. The secondary USB Serial/JTAG console must remain disabled so it does not compete with the native USB-OTG TinyUSB CDC data path.

## 2. Build and flash

From an ESP-IDF terminal:

```powershell
idf.py set-target esp32s3
idf.py reconfigure
idf.py build
idf.py -p COM_FLASH flash monitor
```

The build runs the firmware/hardware contract validator. Evidence-backed settings fail closed when the configured evidence ID is missing or ineligible.

Do not flash a previously generated file from `build/` after firmware source changes. The image currently present in this repository is dated 2026-08-15 and predates the 2026-08-23 Android/CDC logical-session implementation, so it is not compatible with the current integration and must be rebuilt. `tools/build_esp32.ps1` performs a clean build, and the production-build verifier now rejects an ELF or application image older than any relevant firmware input.

## 3. Do not confuse the board's USB connectors

The board reference has two conceptually different USB paths:

- **Programming/diagnostic USB-UART:** used from the development PC to flash or monitor the board.
- **Native ESP32-S3 USB:** GPIO19 D- / GPIO20 D+, used by the Shahbaz Protocol v2 data link.

The operational connection is:

```text
Native ESP32-S3 USB <-> Android phone USB Host <-> Shahbaz application
```

Windows may connect to the same native USB data interface only for development/HIL diagnostics.

## 4. Windows development check

For board-level diagnostics, Windows should expose the native USB CDC interface as a COM port (`COM_USB`). This is **not** an operational product requirement; it is a development gate used by document 06.

## 5. Android operational check

On the real product path, the Android phone should enumerate the board as a USB device. The Shahbaz app must request/hold USB permission and open the CDC bulk endpoints. A Windows COM-port name is irrelevant on Android.

The integration adapter in `android_reference/src/android/kotlin/com/shahbaz/androidusb/` discovers CDC interfaces by USB class/endpoints rather than by Windows COM naming.

## 6. Power warning

The project requires Android to be the USB Host for data, while USB VBUS must not become an unintended board/sensor/battery power path. Do not use simultaneous external board power and Android USB until the exact physical VBUS/backfeed path is verified as described in documents 10 and 14.

**Next:** `05_CODE_TESTING.en.md`.
