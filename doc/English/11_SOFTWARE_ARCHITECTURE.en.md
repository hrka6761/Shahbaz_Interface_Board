# 11 — Software Architecture

[فارسی](../فارسی/11_SOFTWARE_ARCHITECTURE.fa.md) | **English**

This document explains the hardened runtime structure and the operational Android integration boundary. A normal user does not need it before completing documents 00 through 10.

## Operational product path

```text
validated ESP-IDF I2C adapter
  -> SHT30 / MS5611 drivers
  -> sensor scheduler
  -> normalized SensorSample
  -> telemetry serializer
  -> protocol-v2 frame
  -> native USB CDC transport
  -> Android UsbManager / CDC bulk transport
  -> ShahbazLinkSession
  -> Shahbaz Android application data model/UI
```

Windows HIL implements a separate diagnostic client against the same device protocol and is not part of the operational product path.

The I2C adapter enforces board GPIO policy inside both `initialize()` and `recover()`. `app_main` also blocks I2C initialization when board validation reports an invalid assignment, so bus recovery cannot be used as a back door to toggle a reserved GPIO.

## Command/session path

```text
USB CDC RX
  -> attachment-boundary RX/TX flush
  -> COBS/CRC frame decoder
  -> ProtocolEngine session-token check
  -> sender monotonic-time freshness mapping
  -> CommandDispatcher sequence/schema/state checks
  -> SafetySupervisor
  -> requested subsystem
```

Every physical USB attachment receives a new random 64-bit session token. Session-bound commands from an old attachment are rejected before command dispatch. Time synchronization maps sender monotonic timestamps into device receive time; stale/expired frames are rejected before they can refresh normal heartbeat/control freshness.

## Safe boot order

```text
runtime board/memory/GPIO/evidence validation
  -> SafetySupervisor starts in safe state
  -> latch fatal profile mismatch if present
  -> initialize physical actuator PWM only when validation/evidence passed
  -> initialize I2C only when pin policy passed
  -> initialize USB transport
  -> construct protocol engine with runtime DeviceInfo facts
  -> subscribe app_main to Task Watchdog
  -> enter service loop
```

Constructing the actuator controller no longer touches LEDC hardware. `forceSafe()` before successful actuator initialization changes software state only, which allows board validation to happen before any physical PWM peripheral is configured.

## Runtime liveness

The current composition uses one main service task. `TaskHealthMonitor` records liveness points for safety, USB RX/TX, command processing, sensors, telemetry, and maintenance. Critical unhealthy records latch a safety fault.

The independent whole-loop stall detector is the ESP-IDF Task Watchdog. `app_main` subscribes itself and feeds it only after completing one service iteration. Project defaults use a 2 s timeout with panic/reset. This prevents a permanently blocked main task from silently leaving software execution frozen indefinitely; a flight-critical design should still provide an independent hardware output-enable/kill mechanism.

## Runtime DeviceInfo

`ProtocolEngine` receives a `DeviceRuntimeInfo` snapshot built from the actual board-validation result and actuator initialization state. `DeviceInfoResponse` therefore reports detected flash/PSRAM, board issue mask, supported/active channels, actuator availability, and whether actuation was enabled by configuration instead of advertising hard-coded active capabilities.

## Architectural principles

- I2C ownership and pin authorization are centralized and fail closed.
- Sensor drivers use bounded state machines and timeouts.
- Failure of one sensor must not stop USB/heartbeat or the other sensor.
- Buffers and queues are bounded.
- The parser resynchronizes after a corrupted frame.
- A physical reconnect defines a hard protocol-session boundary.
- Stale sender timestamps and prior-session tokens cannot refresh link/control health.
- Actuators are separated from the sensor path and initialized only after validation/evidence gates.
- Steady-state runtime should not depend on unbounded allocation.
- Firmware/hardware constants and evidence claims are checked by `tools/validate_firmware_contract.py` during supported target builds.

## Important repository areas

```text
main/                       composition and app_main
components/                 firmware implementations
components/platform_espidf/ ESP-IDF-specific adapters
test/host/                  board-independent C++ tests
tools/                      build/test/HIL/contract validators
android_reference/src/main/ framework-independent Kotlin protocol/session core
android_reference/src/android/ Android UsbManager CDC transport + Shahbaz app adapter
```

For the exact v2 message/session format, continue with `12_USB_PROTOCOL.en.md`.
