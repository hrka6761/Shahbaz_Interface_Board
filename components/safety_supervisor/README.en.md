# Safety Supervisor

[فارسی](README.fa.md) | **English**

Owns the fail-safe state machine for link, heartbeat, arming, faults, and actuator state. It prevents motion commands from reaching active outputs unless the required safety conditions are satisfied.

## Main files

- `safety_types.hpp`: states, faults, and protocol-visible safety vocabulary.
- `actuator_controller.hpp`: narrow actuator interface controlled by safety logic.
- `safety_supervisor_interface.hpp`: minimal supervisor contract used by other components.
- `safety_supervisor.hpp` / `src/safety_supervisor.cpp`: link/session/heartbeat handling, arming/disarming, faults, and fail-safe transitions.
- `test/test_safety_supervisor.cpp`: startup, successful and rejected arming, heartbeat timeout, USB disconnect/reconnect, faults, time anomalies, and forced-safe behavior.
- `Kconfig`: safety timing/configuration options.

In normal operation, arming is reachable only when its prerequisites are valid. Link loss, stale heartbeat, emergency stop, actuator failure, critical-service liveness failure, or watchdog-registration/feed failure forces or latches a safe/fault state. Sender-time freshness and v2 session-token checks happen before normal heartbeat/control freshness is updated, so stale/replayed commands cannot make the link look healthy.
