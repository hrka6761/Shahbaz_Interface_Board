# 07 — Android + Shahbaz Integration Test

[فارسی](../فارسی/07_ANDROID_SHAHBAZ_INTEGRATION_TEST.fa.md) | **English**

This is the **operational end-to-end integration test** for the current project stage.

```text
SHT30 + MS5611
      -> I2C
      -> shahbaz_interface_board / ESP32-S3
      -> Native USB CDC / Shahbaz Protocol v2
      -> Android phone (USB Host)
      -> Shahbaz Android application
```

Windows is not part of this data path.

## Required Android integration

The Shahbaz application must integrate equivalent behavior to the provided reference sources:

- `android_reference/src/main/kotlin/com/shahbaz/protocol/ProvisionalProtocol.kt`
- `android_reference/src/main/kotlin/com/shahbaz/protocol/ShahbazLinkSession.kt`
- `android_reference/src/android/kotlin/com/shahbaz/androidusb/ShahbazUsbCdcTransport.kt`
- `android_reference/src/android/kotlin/com/shahbaz/androidusb/ShahbazInterfaceBoardClient.kt`

The Android-framework code uses `UsbManager`, CDC interface discovery, bulk IN/OUT endpoints, real attach/detach handling, Protocol v2 session setup, heartbeat, periodic TimeSync refresh, sensor decoding, and QNH-based altitude.

## 1. USB permission and device selection

1. Connect the board's **native USB** connector to the Android phone using a USB-Host/OTG-capable cable/adapter.
2. The Shahbaz app must detect a compatible CDC device.
3. The app must explicitly request Android USB permission.
4. The app must open the device only after permission is granted.
5. The programming USB-UART connector must not be treated as the operational Shahbaz link.

## 2. Session establishment

After opening USB, the app must:

1. reset all previous parser/session state;
2. send `TimeSyncRequest` using Android monotonic time;
3. receive `TimeSyncResponse`;
4. verify the echoed client timestamp;
5. accept a non-zero session token;
6. request DeviceInfo;
7. start telemetry;
8. begin heartbeat maintenance;
9. refresh TimeSync periodically during long sessions.

## 3. Live sensor validation in Shahbaz

Confirm inside the actual Shahbaz application UI/data model that:

- SHT30 temperature updates;
- SHT30 relative humidity updates;
- MS5611 pressure updates;
- MS5611 internal temperature updates if exposed by the app;
- values remain plausible and update continuously.

## 4. QNH and barometric altitude

The board transmits pressure; it does **not** own the operational QNH.

Set a known QNH in the Shahbaz application and verify:

```text
barometric altitude = f(MS5611 pressure, Shahbaz-app QNH)
```

Changing QNH in the app must change displayed/calculated altitude without changing the raw pressure reported by the board.

## 5. Android disconnect/reconnect

1. Start a healthy Shahbaz session.
2. Disconnect native USB.
3. The app must immediately mark the board disconnected and stop using the old token/parser state.
4. Reconnect and grant permission if Android requests it again.
5. The app must establish a new TimeSync/session.
6. The new session token must be non-zero and different from the previous physical attachment.
7. No stale data or old-session command may be treated as current.

## 6. Lifecycle/error checks

Verify at minimum:

- USB permission denial is handled without a crash;
- USB detach while telemetry is active is handled without a crash;
- app foreground/background transitions do not silently reuse a closed USB connection;
- `SessionMismatch` triggers session re-establishment rather than token reuse;
- `StaleOrExpired` triggers a fresh TimeSync using current monotonic time;
- reconnect resumes fresh sensor telemetry.

## Android + Shahbaz PASS criteria

- [ ] Android detects the native USB CDC device.
- [ ] Shahbaz obtains USB permission and opens the link.
- [ ] Protocol v2 TimeSync succeeds.
- [ ] A non-zero current session token is established.
- [ ] Heartbeat remains healthy.
- [ ] Live SHT30 data reaches the Shahbaz application.
- [ ] Live MS5611 pressure reaches the Shahbaz application.
- [ ] Shahbaz calculates altitude from pressure + app QNH.
- [ ] QNH changes affect altitude but not raw pressure.
- [ ] Physical USB reconnect creates a clean new session.
- [ ] Permission denial/detach/session errors are handled safely.
- [ ] No actuator activates in the default build.

Only after this document passes should the current product stage proceed to complete acceptance.

**Next:** `08_COMPLETE_SYSTEM_ACCEPTANCE.en.md`.
