# 10 — Safety, Power, and Actuators

[فارسی](../فارسی/10_SAFETY_AND_POWER.fa.md) | **English**

## Safe configuration for the current stage

Sensor and USB testing must be performed with physical actuation disabled:

```text
CONFIG_SHAHBAZ_ACTUATORS_ENABLE=n
```

Motors, ESCs, servos, and propellers are not required for acceptance of the current sensor/USB stage.

## Power and GPIO rules

- ESP32-S3 GPIO logic operates in the 3.3V domain.
- Do not apply 5V logic directly to SDA/SCL or other ESP32-S3 GPIO pins.
- The sensor setup documented for this project uses 3.3V.
- Board and sensors must share a common ground.
- The exact 5V/VIN/VBUS power path must be verified on the physical YD-ESP32-S3-compatible board before mixed-source powering. Do not combine external power and USB power until the backfeed path on your exact board is understood.

## Heartbeat and fail-safe behavior

Motion commands are gated by `SafetySupervisor`. Loss of USB or a valid heartbeat removes the conditions required for active control. For sensor bench testing, keep actuators completely disabled so the motion path is not involved at all.

## When actuator development begins

- Physically remove propellers.
- Run any Windows board-level actuator HIL only with an explicit actuator-test flag; this is a development test and is not part of the Android operational acceptance path.
- Verify waveform and pulse ranges on the bench with suitable measurement equipment.
- Actuator testing is outside the acceptance scope of the current sensor/USB stage.

**Next technical document:** `11_SOFTWARE_ARCHITECTURE.en.md`.

## Runtime hardening now enforced in code

- Physical actuator peripherals are initialized only **after** board/memory/GPIO/evidence validation succeeds.
- Enabling actuators requires exact-board GPIO review plus an eligible evidence record; the build-time contract validator rejects invented or ineligible evidence IDs.
- Invalid I2C pin assignments fail closed in both `app_main` and the ESP-IDF I2C adapter, including the bus-recovery path.
- USB reconnect or CDC DTR reopen clears transport RX/TX state and protocol session state before a new random session token is admitted.
- Session-bound commands require the current token and a mapped sender timestamp inside the v2 freshness window; stale/replayed traffic cannot refresh heartbeat/control freshness.
- `app_main` is subscribed to the ESP-IDF Task Watchdog. The project default is a 2 s timeout with panic/reset. Service-level liveness is also reported through `TaskHealthMonitor` and can latch `CriticalTaskFailure`.

The watchdog is a software/reset fail-safe, not a substitute for an independent hardware motor-enable/kill path in a flight-critical design.
