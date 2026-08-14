# 01 — Project Overview

[فارسی](../فارسی/01_PROJECT_OVERVIEW.fa.md) | **English**

## Purpose

The component is formally named **`shahbaz_interface_board`**. It is an ESP32-S3 N16R8 hardware interface between the drone-side sensors/optional actuators and the **Shahbaz Android application**.

The current operational path is:

```text
SHT30 --------\
               -> I2C -> shahbaz_interface_board / ESP32-S3
MS5611 -------/                         |
                                        | Native USB CDC
                                        | Shahbaz Protocol v2
                                        v
                               Android phone / USB Host
                                        |
                                        v
                               Shahbaz Android application
```

The development-only validation path is separate:

```text
shahbaz_interface_board -> Native USB -> Windows PC -> Windows HIL
                                                   [development/diagnostics only]
```

## Responsibilities

`shahbaz_interface_board` is responsible for:

- acquiring SHT30 temperature/humidity and MS5611 pressure/internal temperature;
- validating sensor CRC/calibration/timing and publishing bounded telemetry;
- implementing Shahbaz Protocol v2 over native USB CDC-ACM;
- enforcing session, freshness, heartbeat, safety, and optional actuator rules;
- presenting a deterministic USB Device interface to the Android USB Host.

The Shahbaz Android application is responsible for:

- Android USB permission and attach/detach lifecycle;
- opening the CDC data path;
- TimeSync/session-token negotiation and heartbeat maintenance;
- displaying live sensor values;
- providing current QNH and calculating barometric altitude from pressure;
- product-level reconnect/error UX and any future command UI.

Windows HIL is responsible only for independent board/firmware validation. It is intentionally useful when diagnosing whether a failure belongs to the board/firmware or to Android/app integration.

## Current actuator phase

Motor/ESC/servo support exists in firmware but is disabled by default:

```text
CONFIG_SHAHBAZ_ACTUATORS_ENABLE=n
```

No actuator is required for the Android sensor/USB acceptance described in documents 00–09.

## What can be tested without physical hardware?

Sensor mathematics, CRC/COBS framing, protocol schemas, session logic, scheduling, safety logic, serialization, Python HIL codec behavior, and the framework-independent Kotlin Android session/protocol logic can be tested without the board.

## What still requires real hardware?

- ESP-IDF target build and boot on the actual ESP32-S3.
- Native USB operation on the real board.
- Real SHT30/MS5611 acquisition and I2C electrical validation.
- Windows board-level HIL as an independent diagnostic gate.
- **Android phone + Shahbaz application end-to-end USB/telemetry/reconnect acceptance.**

A Windows HIL PASS is necessary for a well-isolated board validation workflow, but it is **not sufficient** for final product acceptance.

**Next:** `02_HARDWARE_CONNECTIONS.en.md`.
