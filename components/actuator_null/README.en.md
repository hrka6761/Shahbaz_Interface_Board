# Null Actuator

[فارسی](README.fa.md) | **English**

`actuator_null` is the safe actuator implementation used when physical outputs are disabled. It implements the common actuator interface but always reports hardware as unavailable and never drives GPIO or PWM.

## Main files

- `include/shahbaz/actuator/null_actuator_controller.hpp`: safe controller interface implementation.
- `src/null_actuator_controller.cpp`: fail-closed behavior without hardware access.
- `test/test_null_actuator.cpp`: proves that initialization, arming, and commands cannot activate an output.
- `CMakeLists.txt` and `test/CMakeLists.txt`: ESP-IDF registration and host-test build.

When `CONFIG_SHAHBAZ_ACTUATORS_ENABLE=n`, the firmware uses this implementation. Real PWM output is implemented separately in `components/actuator_espidf`.
