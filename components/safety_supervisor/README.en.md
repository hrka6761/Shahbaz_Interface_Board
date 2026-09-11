# Safety Supervisor

[فارسی](README.fa.md) | **English**

Owns the fail-safe state machine for link, heartbeat, arming, faults, and actuator state. It prevents motion commands from reaching active outputs unless the required safety conditions are satisfied.

## Main files

- `safety_types.hpp`: states, faults, and protocol-visible safety vocabulary.
- `actuator_controller.hpp`: narrow actuator interface controlled by safety logic.
- `safety_supervisor_interface.hpp`: minimal supervisor contract used by other components.
- `safety_supervisor.hpp` / `src/safety_supervisor.cpp`: link/session/heartbeat handling, arming/disarming, faults, and fail-safe transitions.
- `test/test_safety_supervisor.cpp`: startup, successful and rejected arming, heartbeat and actuator-command timeouts, USB disconnect/reconnect, faults, time anomalies, and forced-safe behavior.
- `Kconfig`: safety timing/configuration options.

In normal operation, arming is reachable only when its prerequisites are valid. Link loss, stale heartbeat, stale actuator commands while armed, emergency stop, actuator failure, critical-service liveness failure, or watchdog-registration/feed failure forces or latches a safe/fault state. The actuator-command watchdog is independent of heartbeat, so a healthy USB maintenance loop cannot keep old PWM active after the Android flight-control loop stops. Sender-time freshness and v2 session-token checks happen before normal heartbeat/control freshness is updated, so stale/replayed commands cannot make the link look healthy.

`handleActuatorCommand()` also checks the independent control watchdog before
acceptance, even when command dispatch precedes periodic `evaluate()`. At or
after the timeout boundary, a late command forces `ControlCommandTimeout`
failsafe and cannot renew the deadline. Tests cover immediately before, exactly
at, and immediately after that boundary while heartbeat remains fresh.
