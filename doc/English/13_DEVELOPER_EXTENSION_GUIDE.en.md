# 13 — Developer Extension Guide

[فارسی](../فارسی/13_DEVELOPER_EXTENSION_GUIDE.fa.md) | **English**

## Adding a new sensor

Recommended sequence:

1. Define the data contract and units.
2. Keep testable sensor-domain logic separate from the hardware adapter.
3. Verify CRC/compensation/conversion with deterministic test vectors.
4. Use bounded state-machine timing, timeouts, and retries.
5. Extend the sensor scheduler without starving existing sensors.
6. Add the telemetry ABI.
7. Update Python HIL, Kotlin protocol/session code, and the Android integration adapter at the same time.
8. Add unit and integration tests.
9. Add Windows board-level HIL coverage for the new sensor.
10. Add/extend real Android + Shahbaz end-to-end coverage in document 07.
11. Update documents 01, 02, 05, 06, 07, 08, and 12 if the change affects them.

## Development rules

- Buffers and queues must be bounded.
- No infinite retry or wait loops.
- Failure of one subsystem must not stop heartbeat/safety processing.
- Reconnect must reset transport RX/TX, parser, sequence, time mapping, telemetry state, and every other session-specific state before admitting a new random session token.
- Every new physical output must sit behind `SafetySupervisor`.
- Steady-state runtime should not depend on unbounded allocation.
- Every protocol change needs tests on firmware, framework-independent Kotlin session logic, the Android `UsbManager` integration adapter, and the Python Windows-HIL side.
- New session-bound commands must use the v2 session-token prefix and an explicit sender-time freshness policy.
- Hardware/evidence claims must be added to the machine-readable manifests and `tools/validate_firmware_contract.py`; never authorize hardware from a free-form nonempty evidence string alone.
- Physical output initialization must occur only after board/pin/evidence validation.
- Any new potentially blocking service must remain bounded so `app_main` can feed the Task Watchdog within its reviewed timeout.

## Where files belong

- User and developer documentation: `doc/`.
- Raw reference files, datasheets, photos, manifests, and diagram sources: `hardware_reference/`.
- Recorded validation result: `TEST_RESULTS.en.md` / `TEST_RESULTS.fa.md`.

For hardware pins and datasheets, continue with `14_HARDWARE_REFERENCE_AND_DATASHEETS.en.md`.
