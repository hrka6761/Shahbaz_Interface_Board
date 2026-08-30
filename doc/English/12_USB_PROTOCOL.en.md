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
MotorCommand     u8 motor_channel, u16 pulse_us
ServoCommand     u8 servo_channel, u16 pulse_us
ActuatorCommand  u8 actuator_kind, u8 channel, u16 pulse_us
SetControlMode   u8 mode
```

`actuator_kind` values:

```text
1 = motor
2 = servo
```

The current production PWM backend accepts motor pulses from 900 to 2100 us and servo pulses from
500 to 2500 us. It exposes motor channels 0 through 3 and servo channels 0 through 1 when active.
`SetControlMode` currently accepts only mode `0`, the direct PWM command mode used by the Android
flight-controller module.

`Disarm` and `EmergencyStop` stay tokenless safety overrides. They may request the safer state even
if their frame is otherwise noncanonical, but only canonical valid frames advance normal
freshness/sequence state.

While armed, at least one accepted motor, servo, or generic actuator command must refresh the
independent control-command watchdog within 250 ms. Heartbeat and `SetControlMode` traffic do not
refresh it. Expiry immediately drives safe outputs and latches the corresponding safety reason.

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

## SensorSample

Current sensor identifiers:

```text
sensor 1 = SHT30
sensor 2 = MS5611
instance = 0
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

SHT30 reports ambient temperature and relative humidity. MS5611 reports pressure and internal temperature. In the operational product, the **Shahbaz Android application** owns QNH and calculates barometric altitude from pressure. Windows HIL may calculate the same value independently for diagnostics only.

## Required Android/Shahbaz client behavior

- Perform `TimeSyncRequest` after every physical reconnect before session-bound commands.
- Never reuse a session token from a previous attachment.
- Keep sender timestamps monotonic and current.
- Periodically re-synchronize long-lived sessions so clock drift remains well inside the freshness window.
- Treat `SessionMismatch`, `StaleOrExpired`, CRC failure, and sequence errors as hard command rejection, not as permission to retry with stale data.
- On reconnect, discard client-side command/session state as well as device-side state.
- Do not send arm/motor/servo commands unless `DeviceInfoResponse` reports
  `actuator_available=1`, at least four active motor channels, and an application-level arming
  decision has passed.
- Continue sending fresh heartbeat/time-sync traffic and fresh actuator commands while armed. The
  safety supervisor applies independent heartbeat and actuator-command timeouts; heartbeat traffic
  alone cannot keep the last PWM output active.

Any future protocol change must update C++ tests, Kotlin protocol/session tests, the Android integration adapter, Python HIL/self-tests, and this document together. `tools/validate_firmware_contract.py` verifies that the C++, Kotlin, and Python protocol versions cannot silently diverge.

**Developer next:** `13_DEVELOPER_EXTENSION_GUIDE.en.md`.
