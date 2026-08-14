# 03 — Development Tools Setup

[فارسی](../فارسی/03_DEVELOPMENT_TOOLS_SETUP.fa.md) | **English**

This document prepares the **development workstation**. Windows is used here because the provided build/HIL scripts target PowerShell and COM ports. The operational product host remains an Android phone running the Shahbaz application.

## Required workstation tools

| Tool | Purpose |
|---|---|
| ESP-IDF 5.4+ | Build and flash the ESP32-S3 firmware |
| Python 3 | Validators and Windows board-level HIL |
| CMake + C++ compiler | Hardware-independent host tests |
| PowerShell | Provided Windows helper scripts |
| Kotlin compiler + Java | Standalone protocol/session tests |
| Android Studio / Android SDK | Integrate and run the Android USB host adapter inside the Shahbaz app |

## ESP-IDF

Open an ESP-IDF terminal and verify:

```powershell
idf.py --version
```

Then from the project root:

```powershell
idf.py set-target esp32s3
idf.py reconfigure
```

## Python test dependencies

```powershell
py -3 -m pip install -r tools\requirements-test.txt
```

## Kotlin protocol/session tests

```powershell
powershell -ExecutionPolicy Bypass -File tools\run_kotlin_protocol_tests.ps1
```

The standalone Kotlin test does not require the Android SDK because it validates the framework-independent protocol/session layer. The Android-framework transport under `android_reference/src/android/` must be compiled as part of the Shahbaz Android application (or an Android library module) with the Android SDK.

## Android integration prerequisites

The Shahbaz application must be able to:

- use `android.hardware.usb.UsbManager`;
- request USB permission from the user;
- receive USB attach/detach lifecycle events;
- claim the CDC-ACM data interface and bulk endpoints;
- route bytes to the Shahbaz Protocol v2 session layer;
- retain QNH as an application value and update altitude when pressure changes.

Reference integration code is provided under `android_reference/src/android/kotlin/com/shahbaz/androidusb/`.

## Windows HIL role

Windows HIL is an independent diagnostic path. It is deliberately kept because a board that fails Windows HIL should not be debugged first inside the Android application. Conversely, a board that passes Windows HIL but fails Android acceptance points strongly toward USB permission/lifecycle/app integration rather than the sensor firmware.

**Next:** `04_BUILD_FLASH_AND_USB.en.md`.
