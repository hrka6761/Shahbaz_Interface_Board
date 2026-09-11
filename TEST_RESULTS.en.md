# Shahbaz validation record

[فارسی](TEST_RESULTS.fa.md) | **English**

Date: 2026-09-11<br>
Target: `shahbaz_interface_board` / ESP32-S3 N16R8  
Operational host: Android phone running the Shahbaz application  
Development diagnostic host: Windows HIL  
Protocol: Shahbaz wire protocol v2 over native USB CDC-ACM

## Executed in this workspace

- CMake configure/build for `test/host`: **PASS** with warnings treated as errors.
- CTest: **18/18 PASS**, 0 failures, in `build-host-vl53` (C++17, Release). This includes a production USB transport harness, acquisition-window/sensor scheduler tests, late-control watchdog boundaries, and overflow/period-safe PWM regressions. The WinLibs compiler's `bin` directory must be on `PATH` when launching its test executables.
- `tools/windows_hil_test.py --self-test`: **PASS** for the Python diagnostic codec, CRC32C/COBS/framing, SHT30/MS5611 payload validation, session structures, and altitude/QNH math.
- Kotlin Protocol v2 + operational session self-test: **PASS**, including physical-attachment state reset, TimeSync/session-token establishment, blocking session-bound traffic before TimeSync, telemetry decoding, and QNH altitude.
- The standalone Kotlin check covers the framework-independent reference; it does not execute the real Android USB permission/device lifecycle.
- `tools/validate_firmware_contract.py`: **PASS**. In addition to firmware/hardware constants and safety hardening, the validator now enforces the product-role contract: `shahbaz_interface_board` is the USB Device, Android + Shahbaz is the operational host/client, Windows is development/HIL only, and Android integration/acceptance artifacts must exist.
- `tools/check_firmware_safety.py`: **PASS** over 61 production source files. Its optional tool-discovery warnings do not replace the separately executed host and target builds.
- `tools/capture_boot_log.py --self-test` and `tools/verify_production_build.py --self-test`: **PASS**. The former validates synthetic transcripts, not a physical boot.
- ESP-IDF **5.4.4** ESP32-S3 compile/link/image build: **PASS**, using `idf.py -B build-vl53-idf -D SHAHBAZ_ACTUATOR_BACKEND=null build`.
- `tools/verify_production_build.py --build-dir build-vl53-idf --require-build --actuator-backend null`: **PASS** against the resulting ELF, linker map, configuration, component graph and **290,544-byte** application image. Physical actuator/LEDC components remain excluded; no hardware enablement is claimed.
- `tools/validate_graphics_inputs.py`: **PASS with 4 warnings**, all limited to missing physical evidence photos.
- `tools/check_bilingual_docs.py` and its discovery self-test: **PASS**; 82 Markdown files form 41 Persian/English pairs with valid local links and Persian direction/terminology policy.

## Review corrections

USB RX work is bounded to eight chunks per callback/application slice. Expired
unsent TX slots are reclaimed promptly, and expired partial COBS frames are
terminated before subsequent frames even under backpressure. Control timeout
is checked during command acceptance, before a late command can renew it. PWM
configuration rejects periods that cannot hold every accepted pulse, and duty
arithmetic is overflow-safe. These changes have reproducing host regressions;
they do not establish real-time deadlines or physical flight performance.

## Architecture correction implemented

The repository now has one explicit product architecture:

```text
SHT30 + MS5611
 -> shahbaz_interface_board / ESP32-S3
 -> Native USB CDC / Shahbaz Protocol v2
 -> Android phone / USB Host
 -> Shahbaz Android application
 -> live sensor values + pressure/QNH barometric altitude
```

Windows HIL remains a separate board-level diagnostic path and is no longer described as the final operational destination or complete-system acceptance criterion.

The Android integration reference now contains:

- framework-independent Protocol v2 codec;
- `ShahbazLinkSession` for session reset, TimeSync/token, heartbeat/telemetry requests, and QNH altitude;
- real `android.hardware.usb` CDC bulk transport;
- explicit `UsbManager.requestPermission` helper;
- application-facing `ShahbazInterfaceBoardClient`;
- USB-host manifest declaration;
- a dedicated physical `07_ANDROID_SHAHBAZ_INTEGRATION_TEST` acceptance procedure.

## Physical tests still required

This review did not have the real ESP32-S3 board, sensors, Android phone/Shahbaz application runtime, or Windows native-USB HIL hardware. Therefore the following are **not marked as executed**:

- flash/boot of the verified ESP-IDF image on the actual board;
- electrical I2C operation at 400 kHz;
- physical Windows board-level HIL;
- **real Android USB permission/open/attach-detach lifecycle in the Shahbaz application**;
- **real Android + Shahbaz live SHT30/MS5611 end-to-end telemetry acceptance**;
- **real Shahbaz QNH -> barometric-altitude behavior**;
- physical VBUS/backfeed verification;
- physical actuator/watchdog behavior if actuators are ever enabled.

The required order on real hardware is:

1. build/flash the ESP32-S3;
2. run document 06 Windows board-level HIL to isolate the board/firmware path;
3. run document 07 on a real Android phone with the real Shahbaz application;
4. accept the current product stage only with document 08 after both the board-level and Android operational gates pass.
