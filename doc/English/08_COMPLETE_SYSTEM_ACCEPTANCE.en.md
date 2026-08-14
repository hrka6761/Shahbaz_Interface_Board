# 08 — Complete System Acceptance

[فارسی](../فارسی/08_COMPLETE_SYSTEM_ACCEPTANCE.fa.md) | **English**

Use this page to decide whether the current **Shahbaz operational integration stage** is complete.

## A — Code and contracts

- [ ] All C++ host tests pass.
- [ ] Python protocol/HIL codec self-test passes.
- [ ] Kotlin Protocol v2 + Android session self-test passes.
- [ ] Firmware/hardware/product contract validator passes.
- [ ] Structural safety and documentation validators pass.
- [ ] `idf.py build` passes for ESP32-S3.

## B — Board-level validation

- [ ] Firmware flashes and the board boots without unexpected resets.
- [ ] SHT30 is online and stable.
- [ ] MS5611 is online and stable.
- [ ] Windows board-level HIL passes as an **independent diagnostic gate**.

A failure here should be fixed before blaming the Android app. A PASS here does not complete product acceptance.

## C — Android + Shahbaz operational integration

- [ ] A real Android phone acts as USB Host for the board's native USB interface.
- [ ] The Shahbaz application obtains USB permission and opens the CDC data path.
- [ ] Protocol v2 TimeSync and a non-zero current session token succeed.
- [ ] Heartbeat/session maintenance remains healthy.
- [ ] Live SHT30 temperature/humidity reaches the Shahbaz application.
- [ ] Live MS5611 pressure reaches the Shahbaz application.
- [ ] The Shahbaz application calculates barometric altitude from pressure and its QNH value.
- [ ] Changing QNH changes altitude without modifying raw pressure.
- [ ] USB detach/reconnect produces a fresh clean session with no stale-state reuse.
- [ ] Permission denial and lifecycle interruptions are handled safely.

## D — Bench safety

- [ ] `CONFIG_SHAHBAZ_ACTUATORS_ENABLE=n` is used for this stage.
- [ ] No motor, ESC, or servo activates unexpectedly.
- [ ] Sensor power/wiring is 3.3V-compatible with shared ground.
- [ ] The Android VBUS/backfeed arrangement satisfies the verified power-safety requirements before mixed-source operation.

## Final result

The current stage may be accepted only when **A through D all pass**. The accepted product path is:

```text
ESP32-S3 + SHT30 + MS5611 + Native USB
                -> Android phone / USB Host
                -> Shahbaz Android application
```

Windows HIL remains a supporting development/diagnostic result and is never a substitute for section C.

If something fails, continue with `09_TROUBLESHOOTING.en.md`.
