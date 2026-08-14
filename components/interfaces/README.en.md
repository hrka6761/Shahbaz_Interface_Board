# Platform-Independent Interfaces

[فارسی](README.fa.md) | **English**

Defines hardware- and platform-independent contracts so most firmware logic can be tested without ESP-IDF or a physical board.

## Main interfaces

- `byte_view.hpp`: non-owning byte range for allocation-free buffer passing.
- `board_validator.hpp`: runtime board/memory validation contract.
- `i2c_bus.hpp`: transaction boundary used by sensor scheduling/drivers.
- `monotonic_clock.hpp`: monotonic time source for scheduling and timeouts.
- `telemetry_transport.hpp`: framed telemetry byte send/receive contract.
- `sample_publisher.hpp`: output contract for completed sensor samples.

Concrete platform implementations belong in adapter components such as `platform_espidf` and `usb_transport_espidf`.
