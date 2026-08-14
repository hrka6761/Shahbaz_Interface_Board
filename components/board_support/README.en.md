# Board Support and Pin Policy

[فارسی](README.fa.md) | **English**

Central fail-closed configuration policy for the ESP32-S3 N16R8 target. Default I2C is GPIO8/9 and native USB reserves GPIO19/20. Nonexistent GPIO22..25, internal flash/PSRAM-path GPIO26..37, strapping-sensitive pins, diagnostics, and revision-reserved resources are excluded from unreviewed reassignment.

Alternate I2C pins require explicit exact-board review. Physical actuator GPIOs are also accepted only from `AvailableWithReview`, must be unique, and when actuators are enabled require both `CONFIG_SHAHBAZ_ACTUATOR_PINS_PHYSICALLY_REVIEWED=y` and a nonempty evidence record ID. The build-time firmware/hardware contract validator additionally checks that enabled evidence claims resolve to eligible manifest records; an arbitrary string is not sufficient authorization.
