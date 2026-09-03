# Android integration reference

[فارسی](README.fa.md) | **English**

This directory defines the operational client boundary for `shahbaz_interface_board` and is intended to be integrated into the real Shahbaz Android application.

- `src/main/.../ProvisionalProtocol.kt`: bounded Protocol v2 codec and sensor/QNH math.
- `src/main/.../ShahbazLinkSession.kt`: physical-attachment session state, bounded TimeSync correlation, DeviceInfo compatibility validation, heartbeat/telemetry acknowledgement staging, reconnect reset, and QNH altitude.
- `src/android/.../ShahbazUsbCdcTransport.kt`: exact `303A:4001` Android USB-host CDC bulk transport, including full-frame partial-write handling and CDC logical-session line-state edges.
- `src/android/.../ShahbazUsbPermission.kt`: package-scoped mutable Android USB permission request/result helper with requested-device and `UsbManager.hasPermission` reconciliation.
- `src/android/.../ShahbazInterfaceBoardClient.kt`: generation-serialized application composition for bounded initial TimeSync retry, validated one-time session startup, telemetry, heartbeat, periodic TimeSync, disconnect, and QNH.
- `AndroidManifest.integration.xml`: required USB-host feature declaration to merge into the Shahbaz app manifest.

The standalone `tools/run_kotlin_protocol_tests.ps1` intentionally compiles only the framework-independent sources. Android-framework sources require the Android SDK and must be compiled in the Shahbaz application or an Android library module.

## Required integration behavior

- Discover only a device with vendor ID `0x303A`, product ID `0x4001`, a CDC communication interface, and a CDC data interface with bulk IN and OUT endpoints. The reference transport applies all of these checks both while scanning and again before opening.
- Register the permission and detach receivers before scanning so an already-attached board and an attach during registration converge on the same flow. Keep the exact requested `UsbDevice` until its callback is handled. `UsbManager` adds the standard permission extras by filling the supplied `PendingIntent`, so Android 12+ requires `FLAG_MUTABLE`; the intent remains restricted to the application package. The result helper rejects a callback for another device and treats the currently attached matching device plus `UsbManager.hasPermission` as authoritative, even if an OEM omits or mangles the standard granted extra.
- Treat CDC DTR as a logical link edge: opening sends `SET_CONTROL_LINE_STATE(0)`, configures 115200 8N1, then sends `SET_CONTROL_LINE_STATE(DTR)` (`0x0001`, with RTS left clear); closing sends state `0` before releasing the interfaces. Firmware uses only DTR for session state. A bulk OUT transfer may be partial, so the transport advances an offset until the complete Protocol v2 frame is written or a non-positive result fails the link.
- Initial TimeSync is attempted immediately and retried every 500 ms on a finite four-attempt budget. The session retains the bounded set of outstanding request timestamps, so a delayed response matching any retained retry can establish the token; acceptance clears the whole set. Once the first non-zero token is accepted, startup sends `DeviceInfoRequest` and then `Heartbeat`. It waits for a critical, empty `HeartbeatAck` before sending `StartTelemetry`, then waits for a `CommandAck` whose request sequence and application action both match that exact `StartTelemetry` request.
- `DeviceInfoResponse` must also be received, decoded, and accepted before readiness. It must report Protocol v2, ESP32-S3, no fatal issue in mask `0x400F`, no unknown issue outside the known `0x7FFFF` mask, zero active motor/servo channels, and actuator hardware unavailable and disabled by configuration. Known advisory bits in `0x7BFF0`, including the rangefinder evidence/configuration bits, are accepted but retained in `ValidatedDeviceInfo.advisoryIssueMask` and the full raw mask. The client exposes the result through `validatedDeviceInfo` and `Listener.onDeviceInfoValidated`.
- Only validated DeviceInfo plus both acknowledgements emits `SessionReady` and starts periodic maintenance, regardless of DeviceInfo/reply arrival order. Sensor samples received before that readiness edge are policy-rejected and never surfaced to the application. A matching startup `CommandNack`, invalid DeviceInfo, malformed/wrong acknowledgement, or two-second stage timeout fails closed and requires reopen. A periodic TimeSync with the same token only refreshes the mapping and must not emit another readiness event or repeat startup. A different token is rejected until a real detach/attach resets the session.
- Every logical open receives a strictly increasing client generation. The USB reader and all TimeSync/startup/maintenance timers carry that generation, run through the same serialized session lock, and re-check it before state changes or writes. A callback from an older DTR session therefore cannot write to or close a newly opened session.
- `SensorValidity` follows the firmware domain bits exactly: transport `0x01`, CRC `0x02`, calibration `0x04`, timing `0x08`, and plausibility `0x10`.

The application must still own user-facing permission UX, lifecycle registration for USB permission and detach broadcasts, device selection when multiple matching boards exist, foreground reconciliation after broadcasts can be missed, and presentation/storage of sensor/QNH values. `onSessionReady` now means DeviceInfo compatibility, `HeartbeatAck`, and the matching `StartTelemetry` `CommandAck` have all succeeded.

Run the framework-independent compatibility checks from the repository root with:

```powershell
.\tools\run_kotlin_protocol_tests.ps1
```

The standalone self-test covers the shared wire vector, bounded stream decoding, request policy, exact sensor-validity bits, sensor units/QNH, delayed initial TimeSync retry correlation, DeviceInfo decoding/fatal/advisory/unknown/actuator policy, readiness gating for both DeviceInfo arrival orders, Heartbeat-before-StartTelemetry staging, exact StartTelemetry acknowledgement correlation, fail-closed NACK/wrong-ACK handling, one readiness edge per attachment, same-token periodic refresh, and token-change rejection. Android-framework permission, generation races, and USB-transfer behavior must also be exercised in the Shahbaz Android module and on a physical board. See `doc/English/07_ANDROID_SHAHBAZ_INTEGRATION_TEST.en.md` for the physical acceptance procedure.
