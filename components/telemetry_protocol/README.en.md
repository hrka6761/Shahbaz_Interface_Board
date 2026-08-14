# Shahbaz Telemetry Protocol

[فارسی](README.fa.md) | **English**

Allocation-free implementation of **Shahbaz wire protocol v2**. The physical frame format remains a fixed 22-byte little-endian header, a bounded 512-byte payload, CRC-32C integrity, COBS framing, and a zero delimiter. The incremental parser rejects bad frames and resynchronizes at the next delimiter.

Protocol v2 adds session/freshness semantics above this framing layer: session-bound host commands carry a negotiated 64-bit session token, while the device-link/dispatcher layers enforce token, sender-time, sequence, state, and actuator rules. This component intentionally remains limited to framing and message representation.
