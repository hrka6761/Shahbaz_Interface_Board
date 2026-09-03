# 12 — Shahbaz USB Protocol

[فارسی](../فارسی/12_USB_PROTOCOL.fa.md) | **English**

## Transport and version

The firmware uses native USB CDC-ACM. The byte stream carries **Shahbaz wire protocol v2** frames separated with COBS and protected by CRC-32C.

The decoded frame body still uses the fixed 22-byte little-endian header followed by up to 512 bytes of payload and a 4-byte CRC-32C. Protocol v2 is intentionally not wire-compatible with v1 control traffic because v2 adds a negotiated session token to session-bound host commands.

## Frame principles

Every frame carries version, type, priority, sequence, sender monotonic timestamp, payload length, payload, and CRC. A decoder must reject corrupted or malformed frames and resynchronize at the next zero delimiter without rebooting the device.

Sequence numbers are checked within the current logical CDC session. A rejected CRC frame does not consume/commit its sequence number.

## Session establishment

A newly attached USB host does **not** immediately have permission to send session-bound commands. TinyUSB mount only means that the phone configured the USB device; the logical protocol session opens after the host asserts CDC DTR.

1. After claiming the CDC interfaces, the host deasserts and then asserts DTR. A DTR close/open on an already attached cable deliberately creates a new logical session.
2. At every physical mount/unmount or DTR transition, the firmware flushes TinyUSB RX/TX FIFOs, drains its epoch-tagged RX queue, and resets its TX queue, parser, sequence, telemetry-session state, and time mapping. It admits a session only when USB remains mounted, DTR is asserted, and boundary cleanup has completed. RX chunks and queued TX frames carry their connection epoch so callback/main-loop races cannot move traffic into a later session.
3. The firmware creates a fresh non-zero 64-bit session token using the ESP32 hardware RNG.
4. The host sends `TimeSyncRequest` with its monotonic timestamp in both the frame header and the 8-byte request payload.
5. `TimeSyncResponse` returns 32 bytes:

```text
u64 client_send_us
u64 device_rx_us
u64 device_tx_us
u64 session_token
```

6. The host must use that token for every session-bound request until DTR closes or the physical USB connection ends.

A token from an earlier logical session is rejected with `CommandNack` / `SessionMismatch`. The provided Android `ShahbazLinkSession` re-runs time synchronization every 30 seconds to keep host/device clock drift bounded during long sessions; the session token itself does not change while that logical CDC session remains open.

The session token is an anti-stale/session-binding mechanism, **not host authentication or encryption**. It prevents buffered/control traffic from an earlier physical or logical CDC session from being accepted as part of a new session; it does not defend against a malicious host that is already connected and knows the current token.

## Session-bound host commands

The following host-to-device message payloads are prefixed on the wire with:

```text
u64 session_token
```

followed by the logical message payload:

- `StartTelemetry`
- `StopTelemetry`
- `SetSensorRate`
- `Heartbeat`
- `HeartbeatAck`
- `ArmRequest`
- `ArmConfirm`
- `ActuatorCommand`
- `MotorCommand`
- `ServoCommand`
- `SetControlMode`
- `MotorFrameCommand`

`DeviceInfoRequest`, `DeviceStatusRequest`, `Ping`, and `TimeSyncRequest` remain tokenless diagnostic/session-establishment requests.

`EmergencyStop` and `Disarm` deliberately remain tokenless **safety overrides**. Even a noncanonical/stale safety-override frame is allowed to request the safer state, while only a canonical valid frame advances normal freshness/sequence state.

## Sender freshness

After a valid time synchronization, the firmware maps the sender monotonic clock to device receive time. Commands that require synchronized sender time are accepted only when the mapped sender timestamp is within the configured v2 freshness window:

```text
maximum sender age       = 250 ms
maximum future tolerance = 50 ms
```

A stale timestamp, a sender timestamp older than the synchronization baseline, or an excessive future timestamp is rejected as `StaleOrExpired`. A stale/expired frame does not refresh valid-frame, heartbeat, or control-command freshness. A forward-moving `TimeSyncRequest` is allowed to replace an aged mapping even when that old mapping would classify the new synchronization timestamp as stale; a sender-clock regression below the existing synchronization baseline is still rejected. This makes periodic re-synchronization able to repair accumulated clock drift without reopening replay acceptance.

## DeviceInfoResponse v2

`DeviceInfoResponse` is 20 bytes and reports runtime facts instead of hard-coded active capabilities:

```text
u8  protocol_version
u8  target_family                 # 1 = ESP32-S3
u8  supported_motor_channels
u8  supported_servo_channels
u32 detected_flash_bytes
u32 detected_psram_bytes
u32 board_validation_issue_mask
u8  active_motor_channels
u8  active_servo_channels
u8  actuator_available
u8  actuators_enabled_by_config
```

In the default sensor/USB build, the protocol reports the designed logical capacity of 4 motors +
2 servos, while active channel counts are zero and `actuator_available=0`. The default
`SHAHBAZ_ACTUATOR_BACKEND=null` profile excludes physical actuator and LEDC code from the image.
In an authorized actuator-capable build, active channels become 4 motors + 2 servos only after the
LEDC PWM backend initializes successfully behind the board-validation and actuator-evidence gates.

Rangefinder-related `board_validation_issue_mask` bits are:

```text
bit 16 = VL53L0X sensor/XSHUT evidence missing
bit 17 = invalid VL53L0X XSHUT GPIO configuration
bit 18 = VL53L0X XSHUT overlaps an enabled actuator GPIO
```

These bits are evaluated when the rangefinder feature is requested. Their absence in a build where `CONFIG_SHAHBAZ_VL53L0X_ENABLE=n` does not advertise active rangefinders. Protocol v2 has no separate rangefinder-capability byte; clients use the per-role `DeviceStatusResponse` lifecycle and must still treat missing/stale instance telemetry as unavailable for control.

## Actuator commands

Physical PWM output is disabled by default. To enable it, firmware must be built with
`SHAHBAZ_ACTUATOR_BACKEND=espidf`,
`CONFIG_SHAHBAZ_ACTUATORS_ENABLE=y`, `CONFIG_SHAHBAZ_ACTUATOR_PINS_PHYSICALLY_REVIEWED=y`, a
nonempty `CONFIG_SHAHBAZ_ACTUATOR_EVIDENCE_RECORD_ID`, valid unique GPIOs, and valid runtime
flash/PSRAM detection. Android must also opt in with
`HardwareConnectionConfig.allowActuatorCommands=true`.

Arming and actuator commands are session-bound and therefore carry the 8-byte `session_token`
prefix described above. Logical payloads after that prefix:

```text
ArmRequest       empty
ArmConfirm       empty
ServoCommand     u8 servo_channel, u16 pulse_us
SetControlMode   u8 mode
MotorFrameCommand
                 u8 count (=4), then four (u8 channel, u16 pulse_us) entries
```

Legacy compatibility payloads are:

```text
MotorCommand     u8 motor_channel, u16 pulse_us
ActuatorCommand  u8 actuator_kind, u8 channel, u16 pulse_us
actuator_kind    1 = motor, 2 = servo
```

The current production PWM backend accepts motor pulses from 900 to 2100 us and servo pulses from
500 to 2500 us. It exposes motor channels 0 through 3 and servo channels 0 through 1 when active.
`SetControlMode` currently accepts only mode `0`, the direct PWM command mode used by the Android
flight-controller module.

`MotorFrameCommand` is message `0x8014`. Its fixed 13-byte logical payload must contain channel IDs
`0`, `1`, `2`, and `3` exactly once in that canonical order; each little-endian pulse must pass the
configured motor bounds. Firmware validates the complete payload before the actuator backend is
called. A successful application produces exactly one `CommandAck` whose request sequence matches
the frame and whose application-action byte is `18`. Backend failure forces all outputs safe,
latches an actuator fault, and produces one `CommandNack` instead of an ACK.

ESP32-S3 LEDC has no cross-channel simultaneous-latch primitive. The backend therefore calculates
and stages all four duties first, then calls `ledc_update_duty` sequentially. This is a coherent
protocol/application transaction with all-safe failure handling, not a claim of simultaneous
electrical edges on all four outputs.

The earlier `MotorCommand` and motor-targeted `ActuatorCommand` shapes remain recognized only for
wire compatibility. Production/default firmware rejects both before they refresh safety timestamps,
commit a sequence, or reach an actuator. A deliberate migration bench profile can opt in with
`CONFIG_SHAHBAZ_ALLOW_LEGACY_INDIVIDUAL_MOTOR_COMMANDS=y`; it must never be used for flight. The
standalone `ServoCommand` and a generic `ActuatorCommand` targeting a servo remain available.

`Disarm` and `EmergencyStop` stay tokenless safety overrides. They may request the safer state even
if their frame is otherwise noncanonical, but only canonical valid frames advance normal
freshness/sequence state.

While armed, at least one accepted coherent motor frame or servo command must refresh the independent
control-command watchdog within 250 ms. Heartbeat and `SetControlMode` traffic do not refresh it.
Expiry immediately drives safe outputs and latches the corresponding safety reason.

## Main message types

- `DeviceInfoRequest / DeviceInfoResponse`
- `DeviceStatusRequest / DeviceStatusResponse`
- `TimeSyncRequest / TimeSyncResponse`
- `Heartbeat / HeartbeatAck`
- `StartTelemetry / StopTelemetry`
- `SetSensorRate`
- `Ping / Pong`
- `CommandAck / CommandNack`
- actuator/control commands, disabled by default and enabled only by the explicit PWM actuator
  profile described above

Current firmware emits a 10-byte `DeviceStatusResponse`. Bytes 0..5 retain the legacy safety,
communication, telemetry-enabled, actuator-armed, SHT30-online, and MS5611-online fields. Bytes
6..9 append the fixed Ground, Up, Front-left, and Front-right VL53L0X lifecycle values:

```text
0 = disabled or hardware presence unknown
1 = configured and initializing
2 = live
3 = degraded/offline after a configuration, XSHUT, I2C, timeout, or recovery fault
```

Clients remain required to accept the earlier exact 6-byte shape, but absence of the four appended
bytes means lifecycle is unknown, not proof that rangefinders are absent. All other payload lengths
and lifecycle values are malformed. `LIVE` reports driver lifecycle only; each individual sample
must still pass its own range status, validity, quality, sequence, and freshness checks.

## SensorSample

Current sensor identifiers:

```text
sensor 1 = SHT30
sensor 2 = MS5611
sensor 3 = VL53L0X

SHT30/MS5611 instance = 0
VL53L0X instance 0 = Ground/downward
VL53L0X instance 1 = Upward
VL53L0X instance 2 = Front-left
VL53L0X instance 3 = Front-right
```

The SensorSample payload layout itself is unchanged by protocol v2:

```text
u8  sensor_id
u8  instance_id
u32 sample_sequence
u64 monotonic_timestamp_us
u32 validity_flags
u32 quality_flags
u32 health_flags
u8  field_count
repeated field_count times: u8 field_id, u8 field_type, u32 raw_value
```

Field type `1` is signed 32-bit and field type `2` is unsigned 32-bit. Current fields are:

```text
1 = ambient temperature, signed milli-degrees Celsius
2 = relative humidity, unsigned milli-percent
3 = compensated pressure, signed pascal
4 = internal temperature, signed milli-degrees Celsius
5 = distance, unsigned millimetres
6 = raw VL53L0X range status, unsigned
7 = VL53L0X Shahbaz control-eligibility quality, unsigned percent
```

SHT30 reports fields 1 and 2. MS5611 reports fields 3 and 4. In the operational product, the **Shahbaz Android application** owns QNH and calculates barometric altitude from pressure. Windows HIL may calculate the same value independently for diagnostics only.

Each VL53L0X sample reports fields 5, 6, and 7. Every physical sensor starts at 7-bit address `0x29`; firmware uses four independent XSHUT lines to assign runtime addresses `0x30..0x33` in the fixed instance order above. The feature is disabled by default until sensor and XSHUT evidence gates pass.

The raw range status is `RESULT_RANGE_STATUS[6:3]`. Status `0` and `11` are accepted for control. Named failures are `1` sigma, `2` signal, `3` minimum range, `4` phase, and `5` hardware; other values are unknown. Firmware considers only 30 through 2000 mm inclusive control-eligible. Field 7 is currently `100` only when both status and distance pass that policy, otherwise `0`; it is not a measured optical signal-strength percentage.

VL53L0X validity/quality semantics are:

```text
validity bit 0 = TransportValid
validity bit 1 = CrcValid                 # not set for VL53L0X results
validity bit 2 = CalibrationValid
validity bit 3 = TimingValid
validity bit 4 = PlausibilityValid        # only status 0/11 and 30..2000 mm

quality bit 0 = Fresh
quality bit 1 = RecoveredAfterError
quality bit 2 = RateLimited

VL53L0X health bit 0 = invalid range status
VL53L0X health bit 1 = distance outside 30..2000 mm
```

Raw distance/status are published even when plausibility fails so clients can diagnose out-of-range and optical/status failures. Flight-control consumers must require all expected validity bits and acceptable health/status/quality, retain any last-good value only for presentation, and must never silently use a rejected sample for control. Sequences advance independently for each VL53L0X instance. Driver initialization/retry errors are summarized by the per-role lifecycle rather than serialized as samples; any non-`LIVE` lifecycle, absence, or staleness makes that instance unavailable for control.

The Ground instance is only an observation. The board never declares touchdown or disarms from range alone. Android owns tilt projection, continuity/freshness checks, estimator agreement, and fusion with an independent landed indication.

### SetSensorRate logical payload

After the session-token prefix described above, `SetSensorRate` carries:

```text
u8  sensor_id
u8  instance_id
u32 interval_us
```

Sensors 1 and 2 accept only instance 0. Sensor 3 accepts instances 0 through 3 and intervals from 60,000 through 10,000,000 us. All four VL53L0X devices deliberately use one shared fair acquisition cadence: selecting any valid VL53L0X instance updates the array cadence, not only that physical sensor.

## Required Android/Shahbaz client behavior

- Perform `TimeSyncRequest` after every physical reconnect before session-bound commands.
- Never reuse a session token from a previous attachment.
- Keep sender timestamps monotonic and current.
- Periodically re-synchronize long-lived sessions so clock drift remains well inside the freshness window.
- Treat `SessionMismatch`, `StaleOrExpired`, CRC failure, and sequence errors as hard command rejection, not as permission to retry with stale data.
- On reconnect, discard client-side command/session state as well as device-side state.
- Do not send arm/coherent-motor-frame/servo commands unless `DeviceInfoResponse` reports
  `actuator_available=1`, at least four active motor channels, and an application-level arming
  decision has passed.
- Continue sending fresh heartbeat/time-sync traffic and fresh coherent motor frames while armed. The
  safety supervisor applies independent heartbeat and actuator-command timeouts; heartbeat traffic
  alone cannot keep the last PWM output active.

Any future protocol change must update C++ tests, Kotlin protocol/session tests, the Android integration adapter, Python HIL/self-tests, and this document together. `tools/validate_firmware_contract.py` verifies that the C++, Kotlin, and Python protocol versions cannot silently diverge.

**Developer next:** `13_DEVELOPER_EXTENSION_GUIDE.en.md`.
