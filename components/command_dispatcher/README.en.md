# Command Dispatcher

[فارسی](README.fa.md) | **English**

Provides the controlled entry point for decoded protocol requests. It validates envelope shape, per-session sequence order, mapped sender-time freshness, local dispatch age, safety state, and command-specific payload constraints before a request can change application state or actuators.

Session-token verification and token stripping occur one layer earlier in `device_link::ProtocolEngine`; stale or unsynchronized session-bound commands reach the dispatcher with an explicit freshness classification and are rejected without refreshing normal link/control freshness. `EmergencyStop` and `Disarm` remain fail-safe overrides that can always request a safer state.

## Main files

- `include/shahbaz/command/command_dispatcher.hpp`: dispatch API, validation/freshness results, and error vocabulary.
- `src/command_dispatcher.cpp`: sequence, timestamp, state, schema, and safety routing.
- `test/test_command_dispatcher.cpp`: valid, malformed, stale, unsynchronized, duplicate/out-of-order, and safety-restricted cases.
