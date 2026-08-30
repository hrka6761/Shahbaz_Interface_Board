# Shahbaz Interface Board — ESP32-S3 N16R8

[فارسی](README.fa.md) | **English**

> **Documentation:** if this is your first time opening the project, start with [`doc/English/00_START_HERE.en.md`](doc/English/00_START_HERE.en.md). Persian documentation is under `doc/فارسی/` and English documentation is under `doc/English/`; files in each folder are numbered in the exact recommended reading order.

Firmware and Android integration reference for **`shahbaz_interface_board`**, an ESP32-S3 N16R8 interface between the drone-side sensors/optional actuators and the **Shahbaz Android application**. The board is a USB Device; the operational USB Host is the Android phone running Shahbaz. Windows is retained only for development, flashing, diagnostics, and independent HIL validation.

## Current implemented path

```text
SHT30 + MS5611
      |
ESP-IDF I2C master (GPIO8 SDA, GPIO9 SCL, 400 kHz)
      |
SharedSensorScheduler -> SensorTelemetryPublisher
      |
COBS + CRC32C Shahbaz protocol v2
      |
TinyUSB CDC-ACM on native ESP32-S3 USB (GPIO19 D-, GPIO20 D+)
      |
Android phone — operational USB Host
      |
Shahbaz Android application
      +-> live sensor data
      +-> barometric altitude from pressure + app QNH
      +-> Android flight-controller module

Development-only alternate path:
ESP32-S3 native USB -> Windows PC -> Windows HIL

USB RX -> frame decoder -> CommandDispatcher -> SafetySupervisor
                                       |
                            optional LEDC PWM actuators
```

The sensor and USB path is enabled by default. Physical actuators are
**implemented but disabled by default** (`CONFIG_SHAHBAZ_ACTUATORS_ENABLE=n`).
The default `SHAHBAZ_ACTUATOR_BACKEND=null` component profile also excludes the
physical actuator and LEDC components from the firmware graph and linked image.
When the actuator profile is enabled, the board still requires a nonempty
actuator evidence record, unique reviewed GPIOs, and valid runtime memory
validation before it initializes LEDC PWM or reports active channels in
`DeviceInfoResponse`.
When enabled, arming requires a connected USB host, synchronized v2 session, a negotiated nonzero session token, and a fresh heartbeat. USB detach, heartbeat timeout, a 250 ms production timeout without a fresh actuator command while armed, emergency stop, an actuator hardware error, or a latched runtime-health fault immediately forces outputs to the safe stopped state.

Protocol v2 flushes RX/TX/parser state at every physical USB session boundary, rejects old-session control traffic, checks actual sender timestamp freshness, and reports runtime DeviceInfo values. The main task is subscribed to the ESP-IDF Task Watchdog; watchdog timeout is configured to invoke the panic/reset path.

## Hardware defaults

| Function | Default |
|---|---|
| Target | ESP32-S3, 16 MiB flash, 8 MiB octal PSRAM |
| SHT30 | 7-bit I2C address `0x44` |
| GY-63 / MS5611 | 7-bit I2C address `0x77` (CSB low) |
| I2C | SDA GPIO8, SCL GPIO9, 400 kHz |
| Native USB | GPIO19 D-, GPIO20 D+ |
| Motors (optional) | GPIO4/5/6/7, 400 Hz conventional PWM |
| Servos (optional) | GPIO10/11, 50 Hz conventional PWM |
| Heartbeat timeout | project production default `1000 ms` for the 350 ms host cadence; component Kconfig fallback remains fail-closed at `0 ms` |
| Actuator-command timeout | project production default `250 ms`; independent of heartbeat; component Kconfig fallback remains fail-closed at `0 ms` |

For the MS5611, PS must select I2C and CSB must not float. The project default
is CSB low (`0x77`). Use a common ground and 3.3 V-compatible sensor wiring.
Many breakouts include pull-ups, but the actual bus must still have adequate
SDA/SCL pull-ups.

## Development build/flash on Windows with ESP-IDF

Use ESP-IDF **5.4 or newer** (the managed `esp_tinyusb` dependency is pinned to 2.2.1).
From an ESP-IDF PowerShell/terminal in the project root:

```powershell
idf.py set-target esp32s3
idf.py reconfigure
idf.py build
idf.py -p COM_FLASH flash monitor
```

Or run `powershell -ExecutionPolicy Bypass -File tools\build_esp32.ps1 -ActuatorBackend null`
for the safe target build. A future physical build must use `-ActuatorBackend espidf` together with
the matching enabled Kconfig and eligible exact-board actuator evidence. The build path runs
`tools/validate_firmware_contract.py`; backend/Kconfig mismatches and unsupported evidence claims
fail closed.

`esp_tinyusb` is pinned by `components/usb_transport_espidf/idf_component.yml`.
The default USB descriptor uses Espressif's development VID/default TinyUSB PID
and enumerates one CDC-ACM interface. Use the **native USB connector routed to
GPIO19/GPIO20** for the Shahbaz data link. The flashing/console COM port can be
a different port depending on the board.

## Tests

Hardware-independent C++ tests are strict-warning builds (`-Werror` on GCC/
Clang and `/WX` on MSVC):

```powershell
powershell -ExecutionPolicy Bypass -File tools\run_host_tests.ps1
```

Equivalent manual commands:

```powershell
cmake -S test\host -B build-host -DCMAKE_BUILD_TYPE=Release
cmake --build build-host --config Release
ctest --test-dir build-host -C Release --output-on-failure
py -3 tools\windows_hil_test.py --self-test
```

After flashing the board and wiring both sensors, run the non-actuating **board-level diagnostic HIL** test:

```powershell
py -3 -m pip install -r tools\requirements-test.txt
py -3 tools\windows_hil_test.py --port COM_USB
```

or:

```powershell
powershell -ExecutionPolicy Bypass -File tools\run_windows_hil.ps1 -Port COM_USB
```

The Windows HIL test verifies the board/firmware path independently: v2 time synchronization plus a nonzero session token, heartbeat/failsafe recovery, runtime N16R8 DeviceInfo/board status, sensor-rate commands, **valid telemetry from both SHT30 and MS5611**, ping/pong integrity, CRC rejection/resynchronization, and telemetry stop. It is not final product acceptance; continue with the real Android + Shahbaz integration test in `doc/English/07_ANDROID_SHAHBAZ_INTEGRATION_TEST.en.md`.

## Protocol v2 summary

Frames are a `0x00`-delimited COBS stream with a fixed 22-byte little-endian header, payload up to 512 bytes, and CRC-32C. A physical USB attachment gets a fresh random 64-bit session token returned by `TimeSyncResponse`. Session-bound host commands carry that token as an 8-byte wire-payload prefix. The firmware also maps the host monotonic clock to device receive time and rejects synchronized commands older than 250 ms or implausibly more than 50 ms in the future.

USB detach/reconnect resets the transport RX buffer, TinyUSB unread RX data, TX queue, parser, sequence tracker, telemetry session, and time mapping. `EmergencyStop` and `Disarm` remain tokenless safety overrides. The Android `ShahbazLinkSession` refreshes time synchronization every 30 seconds to bound clock drift in long sessions.

`DeviceInfoResponse` now reports detected flash/PSRAM, board-validation issue mask, compiled channel support, active channel counts, actuator availability, and whether physical actuators are enabled by configuration.

The SensorSample payload layout is unchanged in v2:

```text
u8 sensor_id
u8 instance_id
u32 sample_sequence
u64 monotonic_timestamp_us
u32 validity_flags
u32 quality_flags
u32 health_flags
u8 field_count
repeated field_count times: u8 field_id, u8 field_type, u32 raw_value
```

Fields currently used:

- SHT30: `1` ambient temperature signed milli-°C, `2` relative humidity unsigned milli-%.
- MS5611: `3` compensated pressure signed Pa, `4` internal temperature signed milli-°C.

See [`doc/English/12_USB_PROTOCOL.en.md`](doc/English/12_USB_PROTOCOL.en.md) and [`doc/English/11_SOFTWARE_ARCHITECTURE.en.md`](doc/English/11_SOFTWARE_ARCHITECTURE.en.md) for the exact session, payload, and integration behavior.

## Main components

| Component | Responsibility |
|---|---|
| `platform_espidf` | ESP timer, runtime board validation, real ESP-IDF I2C master adapter and bus recovery |
| `sensor_sht30` / `sensor_ms5611` | CRC, conversion, calibration and compensation math |
| `sensor_scheduler` | non-blocking shared-I2C sensor state machines and retry/recovery |
| `telemetry_protocol` | bounded COBS/CRC32C framing and stream parser |
| `device_link` | USB protocol engine, replies, telemetry serializer, end-to-end link composition |
| `command_dispatcher` | schemas, sequence/freshness/state validation |
| `safety_supervisor` | heartbeat/link/arming/failsafe state machine |
| `usb_transport_espidf` | native TinyUSB CDC-ACM RX/TX transport |
| `actuator_espidf` | optional LEDC conventional PWM for 4 motors + 2 servos |
| `actuator_null` | safe unavailable actuator used when physical outputs are disabled |
| `android_reference/src/main` | framework-independent Kotlin Protocol v2 + operational session core |
| `android_reference/src/android` | Android `UsbManager` CDC transport and `ShahbazInterfaceBoardClient` integration adapter |

## Important acceptance boundary

Host/codec tests can run without a board. Windows HIL then validates the physical board, sensors, firmware, native USB, and protocol **independently of Android**. Final acceptance is a separate requirement: a real Android phone running the Shahbaz application must obtain USB permission, establish Protocol v2, receive live SHT30/MS5611 telemetry, calculate altitude from pressure + app QNH, and recover cleanly from USB reconnect. A Windows HIL PASS never substitutes for that Android end-to-end acceptance.
