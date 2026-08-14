# Android integration reference

[فارسی](README.fa.md) | **English**

This directory defines the operational client boundary for `shahbaz_interface_board` and is intended to be integrated into the real Shahbaz Android application.

- `src/main/.../ProvisionalProtocol.kt`: bounded Protocol v2 codec and sensor/QNH math.
- `src/main/.../ShahbazLinkSession.kt`: physical-attachment session state, TimeSync/token handling, heartbeat/telemetry request construction, reconnect reset, and QNH altitude.
- `src/android/.../ShahbazUsbCdcTransport.kt`: real `android.hardware.usb` CDC bulk transport.
- `src/android/.../ShahbazUsbPermission.kt`: explicit Android USB permission request/result helper.
- `src/android/.../ShahbazInterfaceBoardClient.kt`: application-facing composition for session setup, telemetry, heartbeat, periodic TimeSync, disconnect, and QNH.
- `AndroidManifest.integration.xml`: required USB-host feature declaration to merge into the Shahbaz app manifest.

The standalone `tools/run_kotlin_protocol_tests.ps1` intentionally compiles only the framework-independent sources. Android-framework sources require the Android SDK and must be compiled in the Shahbaz application or an Android library module.

The application must still own user-facing permission UX, lifecycle registration for USB permission and detach broadcasts, device selection when multiple CDC devices exist, and presentation/storage of sensor/QNH values. See `doc/English/07_ANDROID_SHAHBAZ_INTEGRATION_TEST.en.md` for the physical acceptance procedure.
