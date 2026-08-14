# Health Monitor

[فارسی](README.fa.md) | **English**

Tracks bounded liveness records for critical runtime services. `app_main` now wires the monitor into the composition root and marks the USB RX/TX, command, sensor, telemetry, safety, and maintenance service points. A critical unhealthy mask is latched into `SafetySupervisor` as `CriticalTaskFailure`.

Because the current composition remains a single application task, the independent stall detector is the ESP-IDF Task Watchdog: `app_main` subscribes itself and feeds the watchdog only after one complete service iteration. With the project defaults, a two-second TWDT timeout invokes the panic/reset path. The health monitor complements this by providing explicit service-level diagnostics; it does not replace the watchdog.

## Main files

- `task_health_monitor.hpp/.cpp`: allocation-free liveness records and deadline evaluation.
- `test/task_health_monitor_test.cpp`: healthy, unseen, stale, clock-regression, and recovery cases.
