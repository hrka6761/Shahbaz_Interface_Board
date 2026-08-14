# 09 — Troubleshooting

[فارسی](../فارسی/09_TROUBLESHOOTING.fa.md) | **English**

Troubleshoot according to the product architecture: **Android + Shahbaz is the operational path; Windows HIL is the isolation tool.**

## Shahbaz does not detect the board

- Confirm the phone supports USB Host/OTG mode.
- Use the board's native ESP32-S3 USB connector, not the separate USB-UART programming connector.
- Use a data-capable USB cable/adapter.
- Confirm the Shahbaz app is using `UsbManager` and can see a CDC data interface with bulk IN/OUT endpoints.
- If Android asks for USB permission, grant it; the app must not call `openDevice` before permission is available.
- Disconnect/reconnect after clearing any stale app-side device reference.

If the board still fails in Android, run document 06 Windows HIL. If Windows HIL also fails, debug the board/firmware/USB path first. If Windows HIL passes, focus on Android permission, interface claiming, lifecycle, and Shahbaz integration.

## Android USB permission is denied

The app must remain disconnected and must not retry privileged transfers in a tight loop. Present permission UX again only through the normal Android flow. A permission denial is not a protocol failure.

## Android opens USB but no telemetry appears

Check this order:

1. `TimeSyncResponse` is received and echoes the current request timestamp.
2. The returned session token is non-zero.
3. `StartTelemetry` is sent with that token.
4. Heartbeats use the same current token.
5. Sensor frames decode without CRC/schema errors.

Use Windows HIL to determine whether the board is publishing valid SHT30/MS5611 data independently of the app.

## Shahbaz reports `SessionMismatch`

Never reuse a token across physical USB reconnects. Clear parser/session state on detach, perform a new TimeSync, and use only the newly returned non-zero token.

## Shahbaz reports `StaleOrExpired`

Use Android monotonic time (`SystemClock.elapsedRealtimeNanos`) for sender timestamps. Perform a fresh TimeSync using the current monotonic timestamp and refresh TimeSync periodically during long sessions. Do not replay an older TimeSync request.

## Android disconnect/reconnect does not recover

- Treat `ACTION_USB_DEVICE_DETACHED` as a hard physical session boundary.
- Close `UsbDeviceConnection` and release interfaces.
- Reset the Protocol v2 accumulator, sequence/session token, and time-sync state.
- On reconnect, obtain permission again if necessary and establish a new session before sending session-bound requests.

The provided Android integration code follows these rules.

## Pressure is valid but altitude is wrong

Verify raw MS5611 pressure first. Then verify the **QNH value inside the Shahbaz application**. `1013.25 hPa` is only a standard reference. Changing QNH should change calculated altitude while raw pressure remains unchanged.

## Windows HIL fails

Use Windows HIL only to isolate the lower layers:

- confirm `COM_USB` is the native USB CDC interface;
- no other program has it open;
- check heartbeat timeout configuration;
- check GPIO19/20 native USB routing on the actual board;
- check sensor wiring/I2C addresses/pull-ups.

## `idf.py` or host tests fail

Run the code/contract tests in document 05 before hardware integration. An Android symptom should not be debugged around a known firmware build/test failure.

## Board resets or sensors disappear

- check board power and USB cabling;
- avoid unverified simultaneous external power + USB VBUS paths;
- inspect reset reason;
- verify 3.3V sensor supply, common ground, GPIO8 SDA, GPIO9 SCL;
- verify SHT30 `0x44` and project MS5611 `0x77` configuration.

For electrical constraints see documents 10 and 14.
