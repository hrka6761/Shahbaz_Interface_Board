# 06 — Windows Board-Level HIL Test

[فارسی](../فارسی/06_WINDOWS_BOARD_HIL_TEST.fa.md) | **English**

This is an **independent board/firmware diagnostic test**. It is deliberately run on Windows so the ESP32-S3, sensors, native USB transport, and Protocol v2 can be validated without involving Android application code.

It verifies this development path:

```text
SHT30 + MS5611 -> I2C -> shahbaz_interface_board -> Native USB CDC -> Windows HIL
```

**This is not the product's end-to-end acceptance path.** A HIL PASS means the board/firmware path is healthy enough to continue to the Android + Shahbaz test in document 07.

## Prerequisites

- Document 05 tests pass.
- Firmware is built and flashed.
- Both sensors are connected correctly.
- Actuators are disabled.
- The native USB CDC interface appears as a Windows COM port (`COM_USB`).
- No other program has that COM port open.

## Run HIL

```powershell
py -3 -m pip install -r tools\requirements-test.txt
py -3 tools\windows_hil_test.py --port COM_USB --qnh-hpa 1013.25
```

or:

```powershell
powershell -ExecutionPolicy Bypass -File tools\run_windows_hil.ps1 -Port COM_USB -QnhHpa 1013.25
```

## What HIL verifies

- bidirectional native USB CDC transport;
- Protocol v2 TimeSync and non-zero per-attachment session token;
- heartbeat/failsafe behavior;
- runtime DeviceInfo/status;
- start/stop telemetry and sensor-rate commands;
- valid SHT30 temperature/humidity;
- valid MS5611 pressure/internal temperature;
- independent barometric-altitude calculation from pressure + test QNH;
- ping/pong;
- CRC rejection and stream resynchronization;
- USB disconnect/reconnect with a fresh session token and no stale-session reuse.

## Board-level PASS criteria

- [ ] Board boots without unexpected resets.
- [ ] `COM_USB` appears reliably for diagnostic use.
- [ ] Windows HIL completes without error.
- [ ] SHT30 samples are valid and update.
- [ ] MS5611 pressure samples are valid and update.
- [ ] Disconnect/reconnect creates a fresh Protocol v2 session.
- [ ] No physical actuator activates.

A PASS here is **not** final product acceptance. Continue to the actual operational host.

**Next:** `07_ANDROID_SHAHBAZ_INTEGRATION_TEST.en.md`.
