# 12 — Shahbaz USB Protocol

[فارسی](../فارسی/12_USB_PROTOCOL.fa.md) | **English**

## Transport and version

The firmware uses native USB CDC-ACM. The byte stream carries **Shahbaz wire protocol v2** frames separated with COBS and protected by CRC-32C.

The decoded frame body still uses the fixed 22-byte little-endian header followed by up to 512 bytes of payload and a 4-byte CRC-32C. Protocol v2 is intentionally not wire-compatible with v1 control traffic because v2 adds a negotiated session token to session-bound host commands.

## Frame principles

Every frame carries version, type, priority, sequence, sender monotonic timestamp, payload length, payload, and CRC. A decoder must reject corrupted or malformed frames and resynchronize at the next zero delimiter without rebooting the device.

Sequence numbers are checked within the current physical USB session. A rejected CRC frame does not consume/commit its sequence number.

## Session establishment

A newly attached USB host does **not** immediately have permission to send session-bound commands.

1. The firmware flushes TinyUSB unread RX bytes, its own RX stream buffer, TX queue, parser state, sequence state, telemetry-session state, and time mapping at the attachment boundary.
2. The firmware creates a fresh non-zero 64-bit session token using the ESP32 hardware RNG.
3. The host sends `TimeSyncRequest` with its monotonic timestamp in both the frame header and the 8-byte request payload.
4. `TimeSyncResponse` returns 32 bytes:

```text
u64 client_send_us
u64 device_rx_us
u64 device_tx_us
u64 session_token
```

5. The host must use that token for every session-bound request until the physical USB session ends.

A token from a previous attachment is rejected with `CommandNack` / `SessionMismatch`. The provided Android `ShahbazLinkSession` re-runs time synchronization every 30 seconds to keep host/device clock drift bounded during long sessions; the session token itself does not change during that physical attachment.

The session token is an anti-stale/session-binding mechanism, **not host authentication or encryption**. It prevents buffered/control traffic from an earlier physical attachment from being accepted as part of a new session; it does not defend against a malicious host that is already connected and knows the current token.

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

In the default sensor/USB build, supported channels remain 4 motors + 2 servos as a compiled capability, while active channel counts are zero and `actuator_available=0` because physical actuation is disabled.

## Main message types

- `DeviceInfoRequest / DeviceInfoResponse`
- `DeviceStatusRequest / DeviceStatusResponse`
- `TimeSyncRequest / TimeSyncResponse`
- `Heartbeat / HeartbeatAck`
- `StartTelemetry / StopTelemetry`
- `SetSensorRate`
- `Ping / Pong`
- `CommandAck / CommandNack`
- actuator/control commands, disabled by default in the current sensor bench configuration

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

Any future protocol change must update C++ tests, Kotlin protocol/session tests, the Android integration adapter, Python HIL/self-tests, and this document together. `tools/validate_firmware_contract.py` verifies that the C++, Kotlin, and Python protocol versions cannot silently diverge.

**Developer next:** `13_DEVELOPER_EXTENSION_GUIDE.en.md`.
