# 05 — Code and Contract Testing

[فارسی](../فارسی/05_CODE_TESTING.fa.md) | **English**

Run these tests before using either the Windows HIL path or the Android operational path.

## C++ host tests

```powershell
powershell -ExecutionPolicy Bypass -File tools\run_host_tests.ps1
```

Equivalent manual commands:

```powershell
cmake -S test\host -B build-host -DCMAKE_BUILD_TYPE=Release
cmake --build build-host --config Release
ctest --test-dir build-host -C Release --output-on-failure
```

## Python protocol/HIL codec self-test

```powershell
py -3 tools\windows_hil_test.py --self-test
```

This command does not require Windows hardware despite the file name; it checks the shared Python protocol codec and sensor/QNH math used by the diagnostic HIL client.

## Kotlin operational-client session test

```powershell
powershell -ExecutionPolicy Bypass -File tools\run_kotlin_protocol_tests.ps1
```

This test covers Protocol v2 framing/CRC/COBS, session-token negotiation structures, USB-session reset behavior, telemetry decoding, and barometric-altitude calculation used by the Android integration layer.

## Firmware/hardware/product contract validators

```powershell
py -3 tools\validate_firmware_contract.py
py -3 tools\check_firmware_safety.py
py -3 tools\validate_graphics_inputs.py
py -3 tools\check_bilingual_docs.py
```

`validate_firmware_contract.py` also verifies the product architecture contract: `shahbaz_interface_board` is a USB Device, Android + Shahbaz is the operational host/client, Windows is development/HIL only, and the Android integration/acceptance documents exist.

## ESP-IDF target build

On an ESP-IDF workstation:

```powershell
idf.py set-target esp32s3
idf.py reconfigure
idf.py build
```

A host-test PASS does not replace the target build.

## PASS boundary

All code/contract tests must pass before hardware testing. They still do not prove:

- physical USB operation;
- real sensor wiring/electrical quality;
- Android USB permission/lifecycle behavior on an actual phone;
- successful integration into the real Shahbaz application.

Those are covered in documents 06 and 07.

**Next:** `06_WINDOWS_BOARD_HIL_TEST.en.md`.
