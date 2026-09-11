#!/usr/bin/env python3
"""Windows board-level HIL diagnostic for Shahbaz ESP32-S3 USB CDC firmware.

The hardware test is deliberately non-actuating: it validates the wire codec,
synchronization/heartbeat safety path, device identity/status, all configured I2C sensor
telemetry streams, ping/pong, CRC rejection/resynchronization, and clean
telemetry stop.  Actuator/control messages are blocked by this host tool, and a
hardware run only passes when firmware reports that all actuators are disabled.
"""
from __future__ import annotations

import argparse
import contextlib
import dataclasses
import enum
import math
import struct
import sys
import time
from collections import deque
from pathlib import Path
from typing import Deque, Iterable, Optional, TextIO

PROTOCOL_VERSION = 2
HEADER_LEN = 22
MAX_PAYLOAD = 512
HEADER = struct.Struct("<BBHHBBIQH")
CRC = struct.Struct("<I")
DEFAULT_SENSOR_SAMPLE_COUNT = 10
DEFAULT_RECONNECT_TIMEOUT_S = 45.0
RECONNECT_POLL_INTERVAL_S = 0.10
RECONNECT_QUIET_CHECK_S = 0.25
SENDER_STALE_PROBE_AGE_US = 500_000
SENDER_FUTURE_PROBE_OFFSET_US = 5_000_000

# Deliberately broad bench-test limits.  These are not substitutes for the
# drivers' absolute plausibility checks; they catch corrupted/mis-scaled values
# which happen to remain inside those absolute ranges.
SHT30_MAX_TEMPERATURE_JUMP_MDEG_C = 10_000
SHT30_MAX_HUMIDITY_JUMP_MILLI_PERCENT = 25_000
MS5611_MAX_PRESSURE_JUMP_PA = 2_000
MS5611_MAX_TEMPERATURE_JUMP_MDEG_C = 10_000
VL53L0X_MAX_DISTANCE_JUMP_MM = 1_000
RANGEFINDER_ROLES = {
    0: "GROUND",
    1: "UP",
    2: "FRONT_LEFT",
    3: "FRONT_RIGHT",
}


class MessageType(enum.IntEnum):
    DEVICE_INFO_REQUEST = 0x0001
    DEVICE_INFO_RESPONSE = 0x0002
    START_TELEMETRY = 0x0010
    STOP_TELEMETRY = 0x0011
    SET_SENSOR_RATE = 0x0012
    SENSOR_SAMPLE = 0x0020
    DEVICE_STATUS_REQUEST = 0x0030
    DEVICE_STATUS_RESPONSE = 0x0031
    HEARTBEAT = 0x0040
    HEARTBEAT_ACK = 0x0041
    PING = 0x0042
    PONG = 0x0043
    TIME_SYNC_REQUEST = 0x0050
    TIME_SYNC_RESPONSE = 0x0051
    COMMAND_ACK = 0x0060
    COMMAND_NACK = 0x0061
    PROTOCOL_ERROR = 0x0062
    SAFETY_STATE = 0x0063
    EMERGENCY_STOP = 0x0070
    DISARM = 0x0071
    ARM_REQUEST = 0x8000
    ARM_CONFIRM = 0x8001
    ACTUATOR_COMMAND = 0x8010
    MOTOR_COMMAND = 0x8011
    SERVO_COMMAND = 0x8012
    SET_CONTROL_MODE = 0x8013
    MOTOR_FRAME_COMMAND = 0x8014


class Priority(enum.IntEnum):
    CRITICAL = 0
    HIGH = 1
    NORMAL = 2
    LOW = 3


class NackReason(enum.IntEnum):
    MALFORMED_MESSAGE = 0x0001
    UNSUPPORTED_VERSION = 0x0002
    UNKNOWN_MESSAGE_TYPE = 0x0003
    INVALID_LENGTH = 0x0004
    INVALID_STATE = 0x0005
    DUPLICATE_SEQUENCE = 0x0006
    OUT_OF_ORDER_SEQUENCE = 0x0007
    STALE_OR_EXPIRED = 0x0008
    QUEUE_FULL = 0x0009
    SESSION_MISMATCH = 0x000A
    ACTUATORS_NOT_IMPLEMENTED = 0x0100


class ValidationError(enum.IntEnum):
    NONE = 0
    INVALID_ENVELOPE = 1
    INVALID_PAYLOAD_LENGTH = 2
    INVALID_PAYLOAD_VALUE = 3
    UNSUPPORTED_DIRECTION = 4
    LOCAL_DISPATCH_EXPIRED = 5
    SENDER_STALE_OR_EXPIRED = 6
    SENDER_TIME_NOT_SYNCHRONIZED = 7
    SESSION_TOKEN_INVALID = 8
    INVALID_SAFETY_STATE = 9
    DUPLICATE_SEQUENCE = 10
    OUT_OF_ORDER_SEQUENCE = 11
    SUPERVISOR_REJECTED_TIMESTAMP = 12
    ACTUATOR_UNAVAILABLE = 13
    ACTUATOR_REJECTED = 14


SESSION_BOUND_TYPES = frozenset({
    MessageType.START_TELEMETRY,
    MessageType.STOP_TELEMETRY,
    MessageType.SET_SENSOR_RATE,
    MessageType.HEARTBEAT,
    MessageType.HEARTBEAT_ACK,
    MessageType.ARM_REQUEST,
    MessageType.ARM_CONFIRM,
    MessageType.ACTUATOR_COMMAND,
    MessageType.MOTOR_COMMAND,
    MessageType.SERVO_COMMAND,
    MessageType.SET_CONTROL_MODE,
    MessageType.MOTOR_FRAME_COMMAND,
})

# Defense in depth for this sensor/USB HIL.  Keeping the message definitions in
# the codec is required for Protocol v2 compatibility, but this executable must
# never transmit a command which can arm or drive an output.
HIL_FORBIDDEN_TRANSMIT_TYPES = frozenset({
    MessageType.ARM_REQUEST,
    MessageType.ARM_CONFIRM,
    MessageType.ACTUATOR_COMMAND,
    MessageType.MOTOR_COMMAND,
    MessageType.SERVO_COMMAND,
    MessageType.SET_CONTROL_MODE,
    MessageType.MOTOR_FRAME_COMMAND,
})


class SafetyState(enum.IntEnum):
    BOOTING = 0
    DISARMED = 1
    ARMING = 2
    ARMED = 3
    FAILSAFE = 4
    FAULT = 5
    EMERGENCY_STOPPED = 6


class CommunicationState(enum.IntEnum):
    DISCONNECTED = 0
    CONNECTED_INACTIVE = 1
    TRAFFIC_PRESENT_BUT_INVALID = 2
    HEARTBEAT_MISSING = 3
    HEALTHY = 4


class RangefinderLifecycle(enum.IntEnum):
    """Per-role lifecycle appended to an extended DeviceStatusResponse."""

    DISABLED_OR_ABSENT = 0
    INITIALIZING = 1
    LIVE = 2
    DEGRADED = 3


@dataclasses.dataclass(frozen=True)
class Frame:
    message_type: MessageType
    priority: Priority
    sequence: int
    sender_monotonic_us: int
    payload: bytes


@dataclasses.dataclass(frozen=True)
class TimeSyncResult:
    host_send_us: int
    device_receive_us: int
    device_transmit_us: int
    session_token: int


@dataclasses.dataclass(frozen=True)
class PortIdentity:
    device: str
    vid: Optional[int]
    pid: Optional[int]
    serial_number: Optional[str]
    location: Optional[str]


@dataclasses.dataclass(frozen=True)
class CommandNack:
    request_sequence: int
    reason: NackReason
    validation: ValidationError


@dataclasses.dataclass(frozen=True)
class DeviceStatus:
    safety_state: SafetyState
    communication_state: CommunicationState
    telemetry_enabled: bool
    armed: bool
    sht30_online: bool
    ms5611_online: bool
    # None means a legacy six-byte response. It is unknown capability evidence,
    # and must not be interpreted as DISABLED_OR_ABSENT.
    rangefinders: Optional[tuple[RangefinderLifecycle, ...]]


class ProtocolError(RuntimeError):
    pass


def crc32c(data: bytes) -> int:
    crc = 0xFFFFFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0x82F63B78 if (crc & 1) else 0)
    return crc ^ 0xFFFFFFFF


def cobs_encode(data: bytes) -> bytes:
    out = bytearray(b"\x00")
    code_index = 0
    code = 1
    for byte in data:
        if byte == 0:
            out[code_index] = code
            code_index = len(out)
            out.append(0)
            code = 1
        else:
            out.append(byte)
            code += 1
            if code == 0xFF:
                out[code_index] = code
                code_index = len(out)
                out.append(0)
                code = 1
    out[code_index] = code
    return bytes(out)


def cobs_decode(data: bytes) -> bytes:
    if not data:
        raise ProtocolError("empty COBS packet")
    out = bytearray()
    i = 0
    while i < len(data):
        code = data[i]
        if code == 0:
            raise ProtocolError("zero byte inside COBS packet")
        i += 1
        end = i + code - 1
        if end > len(data):
            raise ProtocolError("truncated COBS packet")
        out.extend(data[i:end])
        i = end
        if code != 0xFF and i < len(data):
            out.append(0)
    return bytes(out)


def encode_frame(message_type: MessageType, sequence: int, payload: bytes = b"",
                 priority: Priority = Priority.NORMAL,
                 sender_monotonic_us: Optional[int] = None) -> bytes:
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("payload too large")
    if not 0 <= sequence <= 0xFFFFFFFF:
        raise ValueError("sequence outside uint32")
    if sender_monotonic_us is None:
        sender_monotonic_us = time.monotonic_ns() // 1000
    header = HEADER.pack(PROTOCOL_VERSION, HEADER_LEN, int(message_type), 0,
                         int(priority), 0, sequence, sender_monotonic_us,
                         len(payload))
    decoded = header + payload
    return cobs_encode(decoded + CRC.pack(crc32c(decoded))) + b"\x00"


def decode_frame(encoded_without_delimiter: bytes) -> Frame:
    raw = cobs_decode(encoded_without_delimiter)
    if len(raw) < HEADER_LEN + CRC.size:
        raise ProtocolError("frame too short")
    body, received_crc_bytes = raw[:-4], raw[-4:]
    received_crc = CRC.unpack(received_crc_bytes)[0]
    if crc32c(body) != received_crc:
        raise ProtocolError("CRC32C mismatch")
    if len(body) < HEADER_LEN:
        raise ProtocolError("header missing")
    (version, header_len, message_type, flags, priority, reserved, sequence,
     sender_us, payload_len) = HEADER.unpack(body[:HEADER_LEN])
    if version != PROTOCOL_VERSION or header_len != HEADER_LEN:
        raise ProtocolError("unsupported header/version")
    if flags != 0 or reserved != 0:
        raise ProtocolError("nonzero flags/reserved")
    payload = body[HEADER_LEN:]
    if len(payload) != payload_len or payload_len > MAX_PAYLOAD:
        raise ProtocolError("payload length mismatch")
    try:
        mt = MessageType(message_type)
        pr = Priority(priority)
    except ValueError as exc:
        raise ProtocolError(f"unknown message/priority: {exc}") from exc
    return Frame(mt, pr, sequence, sender_us, payload)


def parse_command_nack(frame: Frame) -> CommandNack:
    if frame.message_type != MessageType.COMMAND_NACK:
        raise ProtocolError(f"expected COMMAND_NACK, got {frame.message_type.name}")
    if len(frame.payload) != 8:
        raise ProtocolError(f"COMMAND_NACK payload must be 8 bytes, got {len(frame.payload)}")
    request_sequence, raw_reason, raw_validation = struct.unpack("<IHH", frame.payload)
    try:
        reason = NackReason(raw_reason)
        validation = ValidationError(raw_validation)
    except ValueError as exc:
        raise ProtocolError(f"COMMAND_NACK contains unknown reason/validation: {exc}") from exc
    return CommandNack(request_sequence, reason, validation)


def parse_device_status(frame: Frame) -> DeviceStatus:
    """Decode legacy 6-byte or lifecycle-extended 10-byte device status."""
    if frame.message_type != MessageType.DEVICE_STATUS_RESPONSE:
        raise ProtocolError(
            f"expected DEVICE_STATUS_RESPONSE, got {frame.message_type.name}"
        )
    if len(frame.payload) not in (6, 10):
        raise ProtocolError(
            f"DEVICE_STATUS_RESPONSE payload must be 6 or 10 bytes, "
            f"got {len(frame.payload)}"
        )
    try:
        safety_state = SafetyState(frame.payload[0])
        communication_state = CommunicationState(frame.payload[1])
    except ValueError as exc:
        raise ProtocolError(f"DEVICE_STATUS_RESPONSE contains unknown state: {exc}") from exc
    boolean_values = frame.payload[2:6]
    if any(value not in (0, 1) for value in boolean_values):
        raise ProtocolError(
            "DEVICE_STATUS_RESPONSE boolean fields must be encoded as 0 or 1"
        )
    rangefinders: Optional[tuple[RangefinderLifecycle, ...]] = None
    if len(frame.payload) == 10:
        try:
            rangefinders = tuple(
                RangefinderLifecycle(value) for value in frame.payload[6:10]
            )
        except ValueError as exc:
            raise ProtocolError(
                f"DEVICE_STATUS_RESPONSE contains unknown rangefinder lifecycle: {exc}"
            ) from exc
    return DeviceStatus(
        safety_state=safety_state,
        communication_state=communication_state,
        telemetry_enabled=bool(boolean_values[0]),
        armed=bool(boolean_values[1]),
        sht30_online=bool(boolean_values[2]),
        ms5611_online=bool(boolean_values[3]),
        rangefinders=rangefinders,
    )


class StreamDecoder:
    def __init__(self) -> None:
        self._packet = bytearray()

    def feed(self, chunk: bytes) -> list[Frame]:
        frames: list[Frame] = []
        for byte in chunk:
            if byte == 0:
                if self._packet:
                    packet = bytes(self._packet)
                    self._packet.clear()
                    frames.append(decode_frame(packet))
            else:
                if len(self._packet) > HEADER_LEN + MAX_PAYLOAD + 16:
                    self._packet.clear()
                    raise ProtocolError("oversize stream packet")
                self._packet.append(byte)
        return frames


def parse_sensor_sample(payload: bytes) -> dict:
    if len(payload) < 27:
        raise ProtocolError("sensor sample shorter than the fixed sensor-sample prefix")
    sensor_id, instance_id = payload[0], payload[1]
    sequence = struct.unpack_from("<I", payload, 2)[0]
    timestamp_us = struct.unpack_from("<Q", payload, 6)[0]
    validity = struct.unpack_from("<I", payload, 14)[0]
    quality = struct.unpack_from("<I", payload, 18)[0]
    health_flags = struct.unpack_from("<I", payload, 22)[0]
    field_count = payload[26]
    expected = 27 + 6 * field_count
    if len(payload) != expected:
        raise ProtocolError(f"sensor sample length {len(payload)} != {expected}")
    fields = {}
    offset = 27
    for _ in range(field_count):
        field_id, field_type = payload[offset], payload[offset + 1]
        raw = struct.unpack_from("<I", payload, offset + 2)[0]
        if field_type == 1:  # Signed32
            value = struct.unpack("<i", struct.pack("<I", raw))[0]
        elif field_type == 2:  # Unsigned32
            value = raw
        else:
            raise ProtocolError(f"unknown field type {field_type}")
        if field_id in fields:
            raise ProtocolError(f"duplicate sensor field {field_id}")
        fields[field_id] = (field_type, value)
        offset += 6
    return {
        "sensor_id": sensor_id, "instance_id": instance_id, "sequence": sequence,
        "timestamp_us": timestamp_us, "validity": validity, "quality": quality,
        "health_flags": health_flags, "fields": fields,
    }


def validate_sensor_sample(sample: dict) -> str:
    sensor_id = sample["sensor_id"]
    instance_id = sample["instance_id"]
    if sensor_id in (1, 2) and instance_id != 0:
        raise ProtocolError("unexpected legacy sensor instance")
    if sensor_id == 3 and instance_id not in RANGEFINDER_ROLES:
        raise ProtocolError(f"unexpected VL53L0X instance {instance_id}")
    # Every published sample must have passed transport CRC, sensor timing and
    # plausibility checks. MS5611 additionally requires valid PROM calibration.
    common_validity = ((1 << 0) | (1 << 2) | (1 << 3) | (1 << 4)
                       if sensor_id == 3 else
                       (1 << 0) | (1 << 1) | (1 << 3) | (1 << 4))
    if (sample["validity"] & common_validity) != common_validity:
        raise ProtocolError(f"sample validity flags incomplete: 0x{sample['validity']:08x}")
    if sensor_id == 3 and (sample["validity"] & (1 << 1)) != 0:
        raise ProtocolError("VL53L0X must not claim an unavailable wire CRC")
    if (sample["quality"] & 1) == 0:
        raise ProtocolError(f"sample is not marked Fresh: 0x{sample['quality']:08x}")
    if sample["quality"] != 1:
        raise ProtocolError(
            f"sample reports recovery/rate-limiting or unknown quality flags: "
            f"0x{sample['quality']:08x}"
        )
    if sample["health_flags"] != 0:
        raise ProtocolError(f"sample reports sensor health faults: 0x{sample['health_flags']:08x}")

    fields = sample["fields"]
    if 8 in fields and fields[8][0] != 2:
        raise ProtocolError("acquisition timing uncertainty must be Unsigned32 microseconds")
    if sample["sensor_id"] == 1:  # SHT30
        if set(fields) not in ({1, 2}, {1, 2, 8}):
            raise ProtocolError(f"SHT30 fields are {sorted(fields)}, expected [1, 2] with optional 8")
        if fields[1][0] != 1 or fields[2][0] != 2:
            raise ProtocolError("SHT30 field types must be temp=Signed32, RH=Unsigned32")
        temp = fields[1][1]
        rh = fields[2][1]
        if not -40_000 <= temp <= 125_000:
            raise ProtocolError(f"SHT30 temperature out of physical range: {temp} mdegC")
        if not 0 <= rh <= 100_000:
            raise ProtocolError(f"SHT30 RH out of physical range: {rh} milli-percent")
        return f"SHT30 temp={temp/1000:.3f} C RH={rh/1000:.3f}%"
    if sample["sensor_id"] == 2:  # MS5611
        if set(fields) not in ({3, 4}, {3, 4, 8}):
            raise ProtocolError(f"MS5611 fields are {sorted(fields)}, expected [3, 4] with optional 8")
        if fields[3][0] != 1 or fields[4][0] != 1:
            raise ProtocolError("MS5611 field types must be pressure=Signed32, temp=Signed32")
        if (sample["validity"] & (1 << 2)) == 0:
            raise ProtocolError("MS5611 sample lacks CalibrationValid")
        pressure = fields[3][1]
        temp = fields[4][1]
        if not 1_000 <= pressure <= 120_000:
            raise ProtocolError(f"MS5611 pressure out of sensor range: {pressure} Pa")
        if not -40_000 <= temp <= 85_000:
            raise ProtocolError(f"MS5611 temperature out of plausible range: {temp} mdegC")
        return f"MS5611 pressure={pressure} Pa temp={temp/1000:.3f} C"
    if sample["sensor_id"] == 3:  # VL53L0X
        if set(fields) not in ({5, 6, 7}, {5, 6, 7, 8}):
            raise ProtocolError(f"VL53L0X fields are {sorted(fields)}, expected [5, 6, 7] with optional 8")
        if any(fields[field_id][0] != 2 for field_id in (5, 6, 7)):
            raise ProtocolError("VL53L0X fields must all be Unsigned32")
        distance, raw_status, signal_quality = (
            fields[5][1], fields[6][1], fields[7][1]
        )
        if not 30 <= distance <= 2_000:
            raise ProtocolError(f"VL53L0X distance outside control range: {distance} mm")
        if raw_status not in (0, 11):
            raise ProtocolError(f"VL53L0X range status is not control-eligible: {raw_status}")
        if signal_quality != 100:
            raise ProtocolError(
                f"VL53L0X valid-evidence quality must be 100, got {signal_quality}"
            )
        return (f"VL53L0X {RANGEFINDER_ROLES[instance_id]} "
                f"distance={distance} mm status={raw_status} quality={signal_quality}%")
    raise ProtocolError(f"unexpected sensor id {sample['sensor_id']}")


def validate_sensor_progression(previous: dict, current: dict) -> None:
    if (current["sensor_id"], current["instance_id"]) != (
            previous["sensor_id"], previous["instance_id"]):
        raise ProtocolError("cannot compare progression from different sensor instances")
    expected_sequence = (previous["sequence"] + 1) & 0xFFFFFFFF
    if current["sequence"] != expected_sequence:
        raise ProtocolError(
            f"sensor {current['sensor_id']} sample sequence gap/regression: "
            f"expected {expected_sequence}, got {current['sequence']}"
        )
    if current["timestamp_us"] <= previous["timestamp_us"]:
        raise ProtocolError(
            f"sensor {current['sensor_id']} timestamp did not advance: "
            f"{previous['timestamp_us']} -> {current['timestamp_us']}"
        )

    old_fields = previous["fields"]
    new_fields = current["fields"]
    if current["sensor_id"] == 1:
        jumps = (
            ("temperature", abs(new_fields[1][1] - old_fields[1][1]),
             SHT30_MAX_TEMPERATURE_JUMP_MDEG_C, "mdegC"),
            ("humidity", abs(new_fields[2][1] - old_fields[2][1]),
             SHT30_MAX_HUMIDITY_JUMP_MILLI_PERCENT, "milli-percent"),
        )
    elif current["sensor_id"] == 2:
        jumps = (
            ("pressure", abs(new_fields[3][1] - old_fields[3][1]),
             MS5611_MAX_PRESSURE_JUMP_PA, "Pa"),
            ("temperature", abs(new_fields[4][1] - old_fields[4][1]),
             MS5611_MAX_TEMPERATURE_JUMP_MDEG_C, "mdegC"),
        )
    elif current["sensor_id"] == 3:
        jumps = (
            ("distance", abs(new_fields[5][1] - old_fields[5][1]),
             VL53L0X_MAX_DISTANCE_JUMP_MM, "mm"),
        )
    else:
        raise ProtocolError(f"unexpected sensor id {current['sensor_id']}")
    for name, delta, maximum, unit in jumps:
        if delta > maximum:
            raise ProtocolError(
                f"sensor {current['sensor_id']} unrealistic {name} jump: "
                f"{delta} {unit} exceeds {maximum} {unit}"
            )


def print_sensor_summary(samples: list[dict]) -> None:
    if not samples:
        raise ValueError("cannot summarize an empty sensor sample series")
    sensor_id = samples[0]["sensor_id"]
    span_s = (samples[-1]["timestamp_us"] - samples[0]["timestamp_us"]) / 1_000_000.0
    common = (
        f"samples={len(samples)} seq={samples[0]['sequence']}..{samples[-1]['sequence']} "
        f"sensor_time_span={span_s:.3f}s transport/sensor_CRC=PASS "
        f"sequence_gaps=0 timestamp_regressions=0 "
        f"health_fault_samples={sum(s['health_flags'] != 0 for s in samples)}"
    )
    if sensor_id == 1:
        temperatures = [s["fields"][1][1] / 1000.0 for s in samples]
        humidities = [s["fields"][2][1] / 1000.0 for s in samples]
        print(
            f"[SUMMARY] SHT30 {common} "
            f"temperature={min(temperatures):.3f}..{max(temperatures):.3f} C "
            f"humidity={min(humidities):.3f}..{max(humidities):.3f}%"
        )
    elif sensor_id == 2:
        pressures = [s["fields"][3][1] for s in samples]
        temperatures = [s["fields"][4][1] / 1000.0 for s in samples]
        print(
            f"[SUMMARY] MS5611 {common} PROM/calibration_CRC=PASS "
            f"pressure={min(pressures)}..{max(pressures)} Pa "
            f"temperature={min(temperatures):.3f}..{max(temperatures):.3f} C"
        )
    elif sensor_id == 3:
        instance_id = samples[0]["instance_id"]
        if instance_id not in RANGEFINDER_ROLES:
            raise ValueError(f"unexpected VL53L0X instance {instance_id}")
        distances = [s["fields"][5][1] for s in samples]
        print(
            f"[SUMMARY] VL53L0X {RANGEFINDER_ROLES[instance_id]} {common} "
            f"distance={min(distances)}..{max(distances)} mm"
        )
    else:
        raise ValueError(f"unexpected sensor id {sensor_id}")


def barometric_altitude_m(pressure_pa: float, qnh_hpa: float = 1013.25) -> float:
    if not math.isfinite(pressure_pa) or pressure_pa <= 0:
        raise ValueError("pressure must be finite and positive")
    if not math.isfinite(qnh_hpa) or not 800.0 <= qnh_hpa <= 1100.0:
        raise ValueError("QNH must be in 800..1100 hPa")
    return 44330.0 * (1.0 - (pressure_pa / (qnh_hpa * 100.0)) ** 0.19029495718363465)


def self_test() -> None:
    vectors = [
        (MessageType.HEARTBEAT, 1, b""),
        (MessageType.PING, 2, b"\x00\x01\x00\x02\x03\x00\x04\xff"),
        (MessageType.SET_SENSOR_RATE, 0xFFFFFFFF, struct.pack("<BBI", 1, 0, 100_000)),
    ]
    decoder = StreamDecoder()
    for mt, seq, payload in vectors:
        wire = encode_frame(mt, seq, payload, sender_monotonic_us=123456789)
        split = len(wire) // 2
        got = decoder.feed(wire[:split]) + decoder.feed(wire[split:])
        assert len(got) == 1
        frame = got[0]
        assert frame.message_type == mt and frame.sequence == seq and frame.payload == payload
    # CRC corruption must be rejected, and the following frame must still decode.
    good = encode_frame(MessageType.PING, 10, b"12345678", sender_monotonic_us=1)
    raw = bytearray(cobs_decode(good[:-1]))
    raw[-1] ^= 0x80
    bad = cobs_encode(bytes(raw)) + b"\x00"
    crc_decoder = StreamDecoder()
    try:
        crc_decoder.feed(bad)
    except ProtocolError as exc:
        assert "CRC32C" in str(exc)
    else:
        raise AssertionError("corrupt CRC was accepted")
    assert crc_decoder.feed(good)[0].payload == b"12345678"

    nack_frame = Frame(
        MessageType.COMMAND_NACK,
        Priority.HIGH,
        99,
        123,
        struct.pack(
            "<IHH",
            0xDEADBEEF,
            int(NackReason.STALE_OR_EXPIRED),
            int(ValidationError.SENDER_STALE_OR_EXPIRED),
        ),
    )
    parsed_nack = parse_command_nack(nack_frame)
    assert parsed_nack == CommandNack(
        0xDEADBEEF,
        NackReason.STALE_OR_EXPIRED,
        ValidationError.SENDER_STALE_OR_EXPIRED,
    )
    for invalid_nack in (
        dataclasses.replace(nack_frame, payload=nack_frame.payload[:-1]),
        dataclasses.replace(nack_frame, payload=struct.pack("<IHH", 1, 0xFFFF, 0)),
        dataclasses.replace(nack_frame, message_type=MessageType.COMMAND_ACK),
    ):
        try:
            parse_command_nack(invalid_nack)
        except ProtocolError:
            pass
        else:
            raise AssertionError("malformed/unknown COMMAND_NACK was accepted")

    legacy_status_frame = Frame(
        MessageType.DEVICE_STATUS_RESPONSE,
        Priority.HIGH,
        100,
        124,
        bytes((SafetyState.DISARMED, CommunicationState.HEALTHY, 1, 0, 1, 0)),
    )
    legacy_status = parse_device_status(legacy_status_frame)
    assert legacy_status.safety_state == SafetyState.DISARMED
    assert legacy_status.communication_state == CommunicationState.HEALTHY
    assert legacy_status.telemetry_enabled and not legacy_status.armed
    assert legacy_status.sht30_online and not legacy_status.ms5611_online
    assert legacy_status.rangefinders is None

    extended_status_frame = dataclasses.replace(
        legacy_status_frame,
        payload=legacy_status_frame.payload + bytes((0, 1, 2, 3)),
    )
    extended_status = parse_device_status(extended_status_frame)
    assert extended_status.rangefinders == (
        RangefinderLifecycle.DISABLED_OR_ABSENT,
        RangefinderLifecycle.INITIALIZING,
        RangefinderLifecycle.LIVE,
        RangefinderLifecycle.DEGRADED,
    )
    for invalid_status in (
        dataclasses.replace(legacy_status_frame, payload=bytes(7)),
        dataclasses.replace(legacy_status_frame, payload=bytes(9)),
        dataclasses.replace(legacy_status_frame, payload=bytes(11)),
        dataclasses.replace(
            extended_status_frame,
            payload=extended_status_frame.payload[:-1] + bytes((4,)),
        ),
        dataclasses.replace(
            legacy_status_frame,
            payload=legacy_status_frame.payload[:2] + bytes((2, 0, 1, 0)),
        ),
        dataclasses.replace(
            legacy_status_frame,
            payload=bytes((7, CommunicationState.HEALTHY, 1, 0, 1, 0)),
        ),
        dataclasses.replace(legacy_status_frame, message_type=MessageType.COMMAND_ACK),
    ):
        try:
            parse_device_status(invalid_status)
        except ProtocolError:
            pass
        else:
            raise AssertionError("malformed DEVICE_STATUS_RESPONSE was accepted")

    # Sample parser signed/unsigned behavior and exact sizing.
    sample = (bytes([1, 0]) + struct.pack("<IQIII", 7, 99, 0x1B, 1, 0) + bytes([2]) +
              struct.pack("<BBi", 1, 1, -1250) + struct.pack("<BBI", 2, 2, 50123))
    parsed = parse_sensor_sample(sample)
    assert parsed["fields"][1][1] == -1250
    assert parsed["fields"][2][1] == 50123
    assert "SHT30" in validate_sensor_sample(parsed)

    ms_sample = (bytes([2, 0]) + struct.pack("<IQIII", 8, 101, 0x1F, 1, 0) + bytes([2]) +
                 struct.pack("<BBi", 3, 1, 101325) + struct.pack("<BBi", 4, 1, 22340))
    parsed_ms = parse_sensor_sample(ms_sample)
    assert parsed_ms["fields"][3][1] == 101325
    assert "MS5611" in validate_sensor_sample(parsed_ms)

    for instance_id, role in RANGEFINDER_ROLES.items():
        range_sample = (
            bytes([3, instance_id]) +
            struct.pack("<IQIII", 20 + instance_id, 200 + instance_id, 0x1D, 1, 0) +
            bytes([3]) +
            struct.pack("<BBI", 5, 2, 500 + instance_id) +
            struct.pack("<BBI", 6, 2, 0) +
            struct.pack("<BBI", 7, 2, 100)
        )
        parsed_range = parse_sensor_sample(range_sample)
        description = validate_sensor_sample(parsed_range)
        assert role in description
        assert parsed_range["fields"][5][1] == 500 + instance_id

    # The field extension leaves old recordings readable for diagnostics.
    # Neither absence nor UINT32_MAX is evidence of a bounded acquisition time.
    for legacy_sample in (parsed, parsed_ms, parsed_range):
        extended = dict(legacy_sample)
        extended["fields"] = dict(legacy_sample["fields"])
        extended["fields"][8] = (2, 10_001)
        validate_sensor_sample(extended)
        extended["fields"][8] = (2, 0xFFFFFFFF)
        validate_sensor_sample(extended)
        extended["fields"][8] = (1, 10_001)
        try:
            validate_sensor_sample(extended)
        except ProtocolError:
            pass
        else:
            raise AssertionError("signed acquisition timing uncertainty was accepted")

    invalid_range = dict(parsed_range)
    invalid_range["fields"] = dict(parsed_range["fields"])
    invalid_range["fields"][6] = (2, 2)
    try:
        validate_sensor_sample(invalid_range)
    except ProtocolError as exc:
        assert "status" in str(exc)
    else:
        raise AssertionError("invalid VL53L0X range status was accepted")

    # Repeated-sample checks require an exact modulo-u32 sequence increment,
    # forward sensor time, and bounded physical movement.
    next_sht = dict(parsed)
    next_sht["fields"] = dict(parsed["fields"])
    next_sht.update(sequence=8, timestamp_us=199)
    next_sht["fields"][1] = (1, -1200)
    next_sht["fields"][2] = (2, 50200)
    validate_sensor_progression(parsed, next_sht)

    bad_sequence = dict(next_sht)
    bad_sequence["sequence"] = 10
    try:
        validate_sensor_progression(next_sht, bad_sequence)
    except ProtocolError as exc:
        assert "sequence" in str(exc)
    else:
        raise AssertionError("sensor sample sequence gap was accepted")

    bad_jump = dict(next_sht)
    bad_jump["fields"] = dict(next_sht["fields"])
    bad_jump.update(sequence=9, timestamp_us=299)
    bad_jump["fields"][2] = (2, next_sht["fields"][2][1] +
                             SHT30_MAX_HUMIDITY_JUMP_MILLI_PERCENT + 1)
    try:
        validate_sensor_progression(next_sht, bad_jump)
    except ProtocolError as exc:
        assert "unrealistic humidity jump" in str(exc)
    else:
        raise AssertionError("unrealistic sensor jump was accepted")

    wrap_previous = dict(parsed_ms)
    wrap_previous.update(sequence=0xFFFFFFFF, timestamp_us=500)
    wrap_current = dict(parsed_ms)
    wrap_current.update(sequence=0, timestamp_us=600)
    validate_sensor_progression(wrap_previous, wrap_current)

    range_previous = parse_sensor_sample(
        bytes([3, 0]) + struct.pack("<IQIII", 0xFFFFFFFF, 700, 0x1D, 1, 0) +
        bytes([3]) + struct.pack("<BBI", 5, 2, 600) +
        struct.pack("<BBI", 6, 2, 11) + struct.pack("<BBI", 7, 2, 100)
    )
    range_current = dict(range_previous)
    range_current["fields"] = dict(range_previous["fields"])
    range_current.update(sequence=0, timestamp_us=800)
    range_current["fields"][5] = (2, 650)
    validate_sensor_progression(range_previous, range_current)

    assert abs(barometric_altitude_m(101325)) < 0.001
    assert 995.0 <= barometric_altitude_m(89875) <= 1005.0
    assert math.isfinite(barometric_altitude_m(101325, 800.0))
    assert math.isfinite(barometric_altitude_m(101325, 1100.0))
    for pressure, qnh in (
        (0.0, 1013.25), (-1.0, 1013.25), (math.nan, 1013.25),
        (math.inf, 1013.25), (-math.inf, 1013.25), (101325.0, 799.99),
        (101325.0, 1100.01), (101325.0, 101325.0),
        (101325.0, math.nan), (101325.0, math.inf),
    ):
        try:
            barometric_altitude_m(pressure, qnh)
        except ValueError:
            pass
        else:
            raise AssertionError(f"invalid altitude inputs were accepted: {pressure=}, {qnh=}")

    # Session-bound commands must fail before TimeSync establishes a token,
    # then carry that token as an exact little-endian prefix.  Diagnostic and
    # safety-override payloads remain tokenless by Protocol v2 design.
    class CaptureLink:
        def __init__(self) -> None:
            self.wires: list[bytes] = []
            self.responses: Deque[Frame] = deque()

        def write(self, wire: bytes) -> None:
            self.wires.append(wire)

        def wait_for(self, expected: Iterable[MessageType], _timeout_s: float) -> Frame:
            if not self.responses:
                raise AssertionError("self-test response queue is empty")
            frame = self.responses.popleft()
            if frame.message_type not in set(expected):
                raise AssertionError(f"unexpected self-test response {frame.message_type.name}")
            return frame

    capture = CaptureLink()
    test_session = Session(capture)  # type: ignore[arg-type]
    unsynchronized_sequence = test_session.sequence
    try:
        test_session.send(MessageType.START_TELEMETRY)
    except ProtocolError as exc:
        assert "requires time-sync/session" in str(exc)
    else:
        raise AssertionError("session-bound command was accepted before TimeSync")
    assert test_session.sequence == unsynchronized_sequence and not capture.wires
    for invalid_token in (0, -1, 0x1_0000_0000_0000_0000):
        try:
            test_session.establish_session(invalid_token)
        except ProtocolError:
            pass
        else:
            raise AssertionError(f"invalid session token was accepted: {invalid_token}")

    negotiated_token = 0x0123456789ABCDEF
    test_session.establish_session(negotiated_token)
    capture.responses.append(nack_frame)
    assert test_session.expect_nack(
        0xDEADBEEF,
        NackReason.STALE_OR_EXPIRED,
        ValidationError.SENDER_STALE_OR_EXPIRED,
    ) == parsed_nack
    application_payload = b"\xa5\x00\x5a"
    for message_type in SESSION_BOUND_TYPES:
        assert test_session._wire_payload(message_type, application_payload) == (
            struct.pack("<Q", negotiated_token) + application_payload
        )
    for message_type in (
        MessageType.TIME_SYNC_REQUEST,
        MessageType.DEVICE_INFO_REQUEST,
        MessageType.PING,
        MessageType.DISARM,
        MessageType.EMERGENCY_STOP,
    ):
        assert test_session._wire_payload(message_type, application_payload) == application_payload

    probe_sequence = test_session.sequence
    assert test_session.send_expected_rejection(
        MessageType.HEARTBEAT,
        sender_monotonic_us=123,
        payload=application_payload,
        priority=Priority.CRITICAL,
    ) == probe_sequence
    probe = decode_frame(capture.wires[-1][:-1])
    assert probe.sequence == probe_sequence and probe.sender_monotonic_us == 123
    assert probe.payload == struct.pack("<Q", negotiated_token) + application_payload
    assert test_session.sequence == probe_sequence

    assert test_session.send(
        MessageType.HEARTBEAT,
        application_payload,
        Priority.CRITICAL,
        sender_monotonic_us=456,
    ) == probe_sequence
    sent = decode_frame(capture.wires[-1][:-1])
    assert sent.sequence == probe_sequence and sent.sender_monotonic_us == 456
    assert sent.payload == struct.pack("<Q", negotiated_token) + application_payload
    assert test_session.sequence == ((probe_sequence + 1) & 0xFFFFFFFF)

    incomplete = test_session.incomplete_session_frame(
        MessageType.HEARTBEAT,
        priority=Priority.CRITICAL,
    )
    assert incomplete and b"\x00" not in incomplete
    incomplete_decoded = decode_frame(incomplete)
    assert incomplete_decoded.sequence == test_session.sequence
    assert incomplete_decoded.payload == struct.pack("<Q", negotiated_token)

    old_token = 0xFEDCBA9876543210
    rollover_sequence = test_session.sequence
    session_mismatch_frame = Frame(
        MessageType.COMMAND_NACK,
        Priority.HIGH,
        100,
        500,
        struct.pack(
            "<IHH",
            rollover_sequence,
            int(NackReason.SESSION_MISMATCH),
            int(ValidationError.SESSION_TOKEN_INVALID),
        ),
    )
    capture.responses.append(session_mismatch_frame)
    assert test_session.send_expected_session_rejection(
        MessageType.HEARTBEAT,
        old_token,
        sender_monotonic_us=789,
        priority=Priority.CRITICAL,
    ) == rollover_sequence
    old_token_probe = decode_frame(capture.wires[-1][:-1])
    assert old_token_probe.sequence == rollover_sequence
    assert old_token_probe.payload == struct.pack("<Q", old_token)
    assert test_session.sequence == rollover_sequence
    assert test_session.expect_nack(
        rollover_sequence,
        NackReason.SESSION_MISMATCH,
        ValidationError.SESSION_TOKEN_INVALID,
    ) == parse_command_nack(session_mismatch_frame)
    capture.responses.append(Frame(
        MessageType.HEARTBEAT_ACK,
        Priority.CRITICAL,
        101,
        501,
        b"",
    ))
    test_session.heartbeat()
    current_token_heartbeat = decode_frame(capture.wires[-1][:-1])
    assert current_token_heartbeat.sequence == rollover_sequence
    assert current_token_heartbeat.payload == struct.pack("<Q", negotiated_token)
    assert test_session.sequence == ((rollover_sequence + 1) & 0xFFFFFFFF)
    try:
        test_session.send_expected_session_rejection(
            MessageType.HEARTBEAT,
            negotiated_token,
        )
    except ProtocolError as exc:
        assert "different token" in str(exc)
    else:
        raise AssertionError("session rejection probe accepted the current token")

    reference_port = PortIdentity("COM7", 0x303A, 0x4001, "ABC123", "1-4")
    same_device_new_com = PortIdentity("COM11", 0x303A, 0x4001, "abc123", "2-9")
    different_device = PortIdentity("COM7", 0x303A, 0x4001, "OTHER", "1-4")
    assert same_physical_port(reference_port, same_device_new_com)
    assert not same_physical_port(reference_port, different_device)
    assert matching_physical_ports(
        reference_port,
        [different_device, same_device_new_com],
    ) == [same_device_new_com]
    location_only = PortIdentity("COM8", 0x303A, 0x4001, None, "1-4")
    assert same_physical_port(location_only, reference_port)

    wire_count = len(capture.wires)
    try:
        test_session.send(MessageType.MOTOR_COMMAND, b"\x00\x84\x03", Priority.CRITICAL)
    except ProtocolError as exc:
        assert "non-actuating HIL" in str(exc)
    else:
        raise AssertionError("HIL transmitter allowed a motor command")
    assert len(capture.wires) == wire_count
    try:
        test_session.send(
            MessageType.MOTOR_FRAME_COMMAND,
            b"\x04\x00\x84\x03\x01\x84\x03\x02\x84\x03\x03\x84\x03",
            Priority.CRITICAL,
        )
    except ProtocolError as exc:
        assert "non-actuating HIL" in str(exc)
    else:
        raise AssertionError("HIL transmitter allowed a coherent motor frame")
    assert len(capture.wires) == wire_count

    print(
        "[PASS] protocol/session-rollover/CRC/COBS/stream/"
        "SHT30/MS5611/altitude self-test"
    )


class SerialLink:
    def __init__(self, serial_port) -> None:
        self.serial = serial_port
        self.decoder = StreamDecoder()
        self.pending: Deque[Frame] = deque()

    def write(self, wire: bytes) -> None:
        written = self.serial.write(wire)
        self.serial.flush()
        if written != len(wire):
            raise RuntimeError(f"short serial write {written}/{len(wire)}")

    def poll(self) -> None:
        waiting = getattr(self.serial, "in_waiting", 0)
        data = self.serial.read(max(1, min(waiting or 1, 4096)))
        if not data:
            return
        for frame in self.decoder.feed(data):
            self.pending.append(frame)

    def wait_for(self, expected: Iterable[MessageType], timeout_s: float) -> Frame:
        expected_set = set(expected)
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            for _ in range(len(self.pending)):
                frame = self.pending.popleft()
                if frame.message_type in expected_set:
                    return frame
                self.pending.append(frame)
            self.poll()
        names = ", ".join(t.name for t in expected_set)
        raise TimeoutError(f"timeout waiting for {names}")

    def require_quiet(self, timeout_s: float, context: str) -> None:
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            self.poll()
            if self.pending:
                frame = self.pending[0]
                raise ProtocolError(
                    f"unexpected {frame.message_type.name} while {context}; "
                    "prior-session RX may have survived reconnect"
                )


class Session:
    def __init__(self, link: SerialLink) -> None:
        self.link = link
        # A time-derived seed lets a second HIL run on the same USB attachment
        # advance in the uint32 serial-number space instead of restarting at 1.
        # The firmware accepts any first sequence after an actual detach/reset.
        self.sequence = int(time.monotonic() * 1000.0) & 0xFFFFFFFF
        self.session_token: Optional[int] = None

    def establish_session(self, token: int) -> None:
        if not 0 < token <= 0xFFFFFFFFFFFFFFFF:
            raise ProtocolError("device returned an invalid zero/out-of-range session token")
        self.session_token = token

    def _wire_payload(self, message_type: MessageType, payload: bytes) -> bytes:
        if message_type not in SESSION_BOUND_TYPES:
            return payload
        if self.session_token is None:
            raise ProtocolError(f"{message_type.name} requires time-sync/session establishment")
        return struct.pack("<Q", self.session_token) + payload

    def _validate_transmit_type(self, message_type: MessageType) -> None:
        if message_type in HIL_FORBIDDEN_TRANSMIT_TYPES:
            raise ProtocolError(
                f"{message_type.name} is blocked by the non-actuating HIL transmitter"
            )

    def send(self, message_type: MessageType, payload: bytes = b"",
             priority: Priority = Priority.NORMAL,
             sender_monotonic_us: Optional[int] = None) -> int:
        self._validate_transmit_type(message_type)
        wire_payload = self._wire_payload(message_type, payload)
        seq = self.sequence
        self.link.write(encode_frame(
            message_type,
            seq,
            wire_payload,
            priority,
            sender_monotonic_us=sender_monotonic_us,
        ))
        self.sequence = (self.sequence + 1) & 0xFFFFFFFF
        return seq

    def send_expected_rejection(self, message_type: MessageType,
                                sender_monotonic_us: int,
                                payload: bytes = b"",
                                priority: Priority = Priority.NORMAL) -> int:
        """Send a probe without consuming the local sequence.

        The caller must require an exact negative acknowledgement and then send
        valid traffic with the same sequence.  This is limited to session-bound,
        non-actuating commands so a mistaken firmware acceptance remains safe.
        """
        self._validate_transmit_type(message_type)
        if message_type not in SESSION_BOUND_TYPES:
            raise ProtocolError("expected-rejection probes must be session-bound")
        wire_payload = self._wire_payload(message_type, payload)
        seq = self.sequence
        self.link.write(encode_frame(
            message_type,
            seq,
            wire_payload,
            priority,
            sender_monotonic_us=sender_monotonic_us,
        ))
        return seq

    def send_expected_session_rejection(self, message_type: MessageType,
                                        rejected_token: int,
                                        sender_monotonic_us: Optional[int] = None,
                                        payload: bytes = b"",
                                        priority: Priority = Priority.NORMAL) -> int:
        """Send safe session-bound traffic with a known-old token.

        Like the freshness probe, this deliberately retains the local sequence
        so exact rejection can be followed by current-token traffic reusing the
        same value.  Actuator/control types remain blocked.
        """
        self._validate_transmit_type(message_type)
        if message_type not in SESSION_BOUND_TYPES:
            raise ProtocolError("session-token rejection probes must be session-bound")
        if self.session_token is None:
            raise ProtocolError("current time-sync/session is not established")
        if not 0 < rejected_token <= 0xFFFFFFFFFFFFFFFF:
            raise ProtocolError("rejected session token is zero/outside uint64")
        if rejected_token == self.session_token:
            raise ProtocolError("session-token rejection probe must use a different token")
        seq = self.sequence
        wire_payload = struct.pack("<Q", rejected_token) + payload
        self.link.write(encode_frame(
            message_type,
            seq,
            wire_payload,
            priority,
            sender_monotonic_us=sender_monotonic_us,
        ))
        return seq

    def incomplete_session_frame(self, message_type: MessageType,
                                 payload: bytes = b"",
                                 priority: Priority = Priority.NORMAL) -> bytes:
        """Build an unterminated current-session frame for stale-RX testing."""
        self._validate_transmit_type(message_type)
        if message_type not in SESSION_BOUND_TYPES:
            raise ProtocolError("stale-RX probe must use a session-bound message")
        wire_payload = self._wire_payload(message_type, payload)
        wire = encode_frame(message_type, self.sequence, wire_payload, priority)
        if not wire.endswith(b"\x00"):
            raise AssertionError("encoded frame lacks delimiter")
        return wire[:-1]

    def expect_ack(self, request_seq: int, timeout_s: float = 2.0) -> Frame:
        frame = self.link.wait_for([MessageType.COMMAND_ACK, MessageType.COMMAND_NACK], timeout_s)
        if frame.message_type == MessageType.COMMAND_NACK:
            nack = parse_command_nack(frame)
            raise RuntimeError(
                f"NACK request={nack.request_sequence} reason={nack.reason.name} "
                f"validation={nack.validation.name}"
            )
        if len(frame.payload) != 5:
            raise ProtocolError("ACK payload must be 5 bytes")
        req, _action = struct.unpack("<IB", frame.payload)
        if req != request_seq:
            raise ProtocolError(f"ACK request sequence {req} != {request_seq}")
        return frame

    def expect_nack(self, request_seq: int, expected_reason: NackReason,
                    expected_validation: ValidationError,
                    timeout_s: float = 2.0) -> CommandNack:
        frame = self.link.wait_for(
            [MessageType.COMMAND_NACK, MessageType.COMMAND_ACK, MessageType.HEARTBEAT_ACK],
            timeout_s,
        )
        if frame.message_type != MessageType.COMMAND_NACK:
            raise ProtocolError(
                f"request {request_seq} was unexpectedly accepted as {frame.message_type.name}"
            )
        nack = parse_command_nack(frame)
        if nack.request_sequence != request_seq:
            raise ProtocolError(
                f"NACK request sequence {nack.request_sequence} != {request_seq}"
            )
        if nack.reason != expected_reason or nack.validation != expected_validation:
            raise ProtocolError(
                f"unexpected NACK for request {request_seq}: reason={nack.reason.name} "
                f"validation={nack.validation.name}"
            )
        return nack

    def heartbeat(self) -> None:
        self.send(MessageType.HEARTBEAT, priority=Priority.CRITICAL)
        frame = self.link.wait_for([MessageType.HEARTBEAT_ACK, MessageType.COMMAND_NACK], 2.0)
        if frame.message_type != MessageType.HEARTBEAT_ACK or frame.payload:
            raise RuntimeError("heartbeat was not acknowledged cleanly")


def establish_time_sync(session: Session) -> TimeSyncResult:
    host_send_us = time.monotonic_ns() // 1000
    session.send(
        MessageType.TIME_SYNC_REQUEST,
        struct.pack("<Q", host_send_us),
        Priority.HIGH,
        sender_monotonic_us=host_send_us,
    )
    sync = session.link.wait_for(
        [MessageType.TIME_SYNC_RESPONSE, MessageType.COMMAND_NACK],
        2.0,
    )
    if sync.message_type != MessageType.TIME_SYNC_RESPONSE or len(sync.payload) != 32:
        raise RuntimeError("time sync failed")
    echoed, device_rx, device_tx, session_token = struct.unpack("<QQQQ", sync.payload)
    if echoed != host_send_us or device_tx < device_rx:
        raise ProtocolError("invalid time-sync response")
    session.establish_session(session_token)
    return TimeSyncResult(host_send_us, device_rx, device_tx, session_token)


def port_identity(port_info) -> PortIdentity:
    serial_number = getattr(port_info, "serial_number", None)
    location = getattr(port_info, "location", None)
    return PortIdentity(
        str(port_info.device),
        getattr(port_info, "vid", None),
        getattr(port_info, "pid", None),
        str(serial_number) if serial_number else None,
        str(location) if location else None,
    )


def same_physical_port(reference: PortIdentity, candidate: PortIdentity) -> bool:
    if reference.vid is not None and candidate.vid != reference.vid:
        return False
    if reference.pid is not None and candidate.pid != reference.pid:
        return False
    if reference.serial_number:
        return (
            candidate.serial_number is not None and
            candidate.serial_number.casefold() == reference.serial_number.casefold()
        )
    if reference.location:
        return (
            candidate.location is not None and
            candidate.location.casefold() == reference.location.casefold()
        )
    return candidate.device.casefold() == reference.device.casefold()


def matching_physical_ports(reference: PortIdentity,
                            candidates: Iterable[PortIdentity]) -> list[PortIdentity]:
    return [candidate for candidate in candidates
            if same_physical_port(reference, candidate)]


def enumerate_port_identities() -> list[PortIdentity]:
    try:
        from serial.tools import list_ports  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "pyserial is required: py -m pip install -r tools\\requirements-test.txt"
        ) from exc
    return [port_identity(info) for info in list_ports.comports()]


def capture_port_identity(port_name: str) -> PortIdentity:
    matches = [identity for identity in enumerate_port_identities()
               if identity.device.casefold() == port_name.casefold()]
    if len(matches) != 1:
        raise RuntimeError(
            f"cannot uniquely identify {port_name} for a reconnect test; "
            f"enumeration matches={len(matches)}"
        )
    identity = matches[0]
    if not identity.serial_number and not identity.location:
        print(
            f"[WARN] {port_name} exposes neither USB serial number nor location; "
            "reconnect matching must fall back to the COM name"
        )
    return identity


def wait_for_physical_port_cycle(reference: PortIdentity, timeout_s: float) -> str:
    if not math.isfinite(timeout_s) or timeout_s <= 0:
        raise ValueError("--reconnect-timeout must be a positive finite number")
    deadline = time.monotonic() + timeout_s
    detached = False
    while time.monotonic() < deadline:
        matches = matching_physical_ports(reference, enumerate_port_identities())
        if len(matches) > 1:
            names = ", ".join(port.device for port in matches)
            raise RuntimeError(f"ambiguous reconnect: matching ports are {names}")
        if not detached:
            if not matches:
                detached = True
                print("[PASS] Windows observed the USB CDC port disappear")
        elif matches:
            print(f"[PASS] Windows observed the same USB device re-enumerate as {matches[0].device}")
            return matches[0].device
        time.sleep(RECONNECT_POLL_INTERVAL_S)
    stage = "re-enumeration" if detached else "real detach/unconfiguration"
    raise TimeoutError(
        f"timeout waiting for USB {stage}; COM close/open and DTR changes do not "
        "create a firmware session boundary"
    )


def select_port(explicit: Optional[str]):
    try:
        import serial  # type: ignore
        from serial.tools import list_ports  # type: ignore
    except ImportError as exc:
        raise RuntimeError("pyserial is required: py -m pip install -r tools\\requirements-test.txt") from exc
    if explicit:
        return explicit, serial
    ports = list(list_ports.comports())
    candidates = [p for p in ports if p.vid == 0x303A]
    if len(candidates) == 1:
        return candidates[0].device, serial
    if not candidates:
        listing = ", ".join(p.device for p in ports) or "none"
        raise RuntimeError(f"no Espressif USB CDC port auto-detected (ports: {listing}); pass --port COMx")
    listing = ", ".join(f"{p.device}(PID={p.pid!s})" for p in candidates)
    raise RuntimeError(f"multiple Espressif USB ports found: {listing}; pass --port COMx")


def corrupt_crc_preserving_cobs(wire: bytes) -> bytes:
    raw = bytearray(cobs_decode(wire[:-1]))
    if len(raw) < 4:
        raise AssertionError("encoded test frame too short")
    raw[-1] ^= 0x01
    return cobs_encode(bytes(raw)) + b"\x00"


def require_sender_freshness_rejection(session: Session, sender_monotonic_us: int,
                                       label: str) -> None:
    request_seq = session.send_expected_rejection(
        MessageType.HEARTBEAT,
        sender_monotonic_us,
        priority=Priority.CRITICAL,
    )
    nack = session.expect_nack(
        request_seq,
        NackReason.STALE_OR_EXPIRED,
        ValidationError.SENDER_STALE_OR_EXPIRED,
    )
    if session.sequence != request_seq:
        raise ProtocolError("expected-rejection probe unexpectedly advanced host sequence")

    # Reuse exactly the rejected sequence with a current timestamp.  A clean
    # HeartbeatAck proves the stale frame did not commit sequence or poison the
    # current-token session.
    session.heartbeat()
    expected_next = (request_seq + 1) & 0xFFFFFFFF
    if session.sequence != expected_next:
        raise ProtocolError("valid recovery heartbeat did not advance host sequence")
    print(
        f"[PASS] {label} rejected request={nack.request_sequence} "
        f"reason={nack.reason.name} validation={nack.validation.name}; "
        "same-sequence fresh heartbeat accepted"
    )


def run_reconnect_session_test(serial_port, port_name: str, serial_module,
                               session: Session, timeout_s: float):
    if session.session_token is None:
        raise ProtocolError("reconnect test requires an established first session")
    token1 = session.session_token
    identity = capture_port_identity(port_name)

    # Leave one complete COBS packet without its delimiter in the old parser/RX
    # path.  After a genuine detach and reattach, sending only the delimiter
    # must remain silent because transport buffers and FrameAccumulator state
    # are both reset at the attachment boundary.
    stale_rx_prefix = session.incomplete_session_frame(
        MessageType.HEARTBEAT,
        priority=Priority.CRITICAL,
    )
    session.link.write(stale_rx_prefix)
    time.sleep(0.05)
    serial_port.close()
    print(
        "[ACTION] cause a real USB detach/re-enumeration now (physical replug, "
        "data-hub port cycle, or approved Windows PnP disable/enable). "
        "Closing/reopening COM or toggling DTR is insufficient."
    )

    new_port_name = wait_for_physical_port_cycle(identity, timeout_s)
    open_deadline = time.monotonic() + min(timeout_s, 5.0)
    new_serial = None
    last_open_error: Optional[BaseException] = None
    while time.monotonic() < open_deadline:
        try:
            new_serial = serial_module.Serial(
                new_port_name,
                baudrate=115200,
                timeout=0.05,
                write_timeout=1.0,
            )
            break
        except (OSError, serial_module.SerialException) as exc:
            last_open_error = exc
            time.sleep(RECONNECT_POLL_INTERVAL_S)
    if new_serial is None:
        raise RuntimeError(
            f"USB CDC re-enumerated as {new_port_name}, but could not be reopened: "
            f"{last_open_error}"
        )

    try:
        time.sleep(0.15)
        new_serial.reset_input_buffer()
        link2 = SerialLink(new_serial)
        link2.write(b"\x00")
        link2.require_quiet(
            RECONNECT_QUIET_CHECK_S,
            "completing the prior attachment's unterminated RX probe",
        )
        print("[PASS] prior-session partial RX was not processed after reconnect")

        session2 = Session(link2)
        sync2 = establish_time_sync(session2)
        if sync2.session_token == token1:
            raise ProtocolError("session token did not change across real USB re-enumeration")
        print("[PASS] reconnect TimeSync returned a distinct nonzero session token")

        request_seq = session2.send_expected_session_rejection(
            MessageType.HEARTBEAT,
            token1,
            priority=Priority.CRITICAL,
        )
        nack = session2.expect_nack(
            request_seq,
            NackReason.SESSION_MISMATCH,
            ValidationError.SESSION_TOKEN_INVALID,
        )
        if session2.sequence != request_seq:
            raise ProtocolError("old-token rejection unexpectedly advanced host sequence")
        session2.heartbeat()
        if session2.sequence != ((request_seq + 1) & 0xFFFFFFFF):
            raise ProtocolError("current-token recovery heartbeat did not advance sequence")
        print(
            f"[PASS] old-token request={nack.request_sequence} rejected "
            f"reason={nack.reason.name} validation={nack.validation.name}; "
            "same-sequence current-token heartbeat accepted"
        )
        return new_serial, new_port_name, link2, session2
    except Exception:
        new_serial.close()
        raise


def run_hardware(port_name: str, serial_module, sensor_timeout_s: float,
                 actuator_test: bool, _props_removed: bool, qnh_hpa: float,
                 sensor_sample_count: int, reconnect_test: bool,
                 reconnect_timeout_s: float,
                 require_rangefinders: bool) -> None:
    if actuator_test:
        raise RuntimeError(
            "--actuator-test is disabled: this HIL is strictly non-actuating and requires "
            "actuators-disabled production firmware"
        )
    if sensor_sample_count < 2:
        raise ValueError("--sensor-samples must be at least 2 for progression checks")
    if not math.isfinite(sensor_timeout_s) or sensor_timeout_s <= 0:
        raise ValueError("--sensor-timeout must be a positive finite number")
    # Validate the configured QNH before opening or commanding the board.
    barometric_altitude_m(101325.0, qnh_hpa)
    ser = serial_module.Serial(port_name, baudrate=115200, timeout=0.05, write_timeout=1.0)
    try:
        time.sleep(0.15)
        ser.reset_input_buffer()
        link = SerialLink(ser)
        session = Session(link)
        print(f"[INFO] opened {port_name} (USB CDC; baud is line-coding only)")

        sync = establish_time_sync(session)
        host_send = sync.host_send_us
        print("[PASS] time synchronization + nonzero session token")

        session.heartbeat()
        print("[PASS] heartbeat and fail-safe recovery")

        # Let the synchronization baseline become older than the production
        # 250 ms sender-age window, while staying well inside the 1 s heartbeat
        # timeout.  Equality with the baseline is intentionally distinct from
        # the next below-baseline regression probe.
        stale_probe_due_us = host_send + SENDER_STALE_PROBE_AGE_US
        remaining_us = stale_probe_due_us - (time.monotonic_ns() // 1000)
        if remaining_us > 0:
            time.sleep(remaining_us / 1_000_000.0)
        require_sender_freshness_rejection(
            session,
            host_send,
            "clearly stale sender timestamp",
        )
        require_sender_freshness_rejection(
            session,
            host_send - 1 if host_send > 0 else 0,
            "sender timestamp below TimeSync baseline",
        )
        now_us = time.monotonic_ns() // 1000
        future_timestamp_us = min(
            0xFFFFFFFFFFFFFFFF,
            now_us + SENDER_FUTURE_PROBE_OFFSET_US,
        )
        require_sender_freshness_rejection(
            session,
            future_timestamp_us,
            "unreasonable future sender timestamp",
        )

        session.send(MessageType.DEVICE_INFO_REQUEST)
        info = link.wait_for([MessageType.DEVICE_INFO_RESPONSE, MessageType.COMMAND_NACK], 2.0)
        if info.message_type != MessageType.DEVICE_INFO_RESPONSE or len(info.payload) != 20:
            raise RuntimeError("device-info request failed")
        (proto, target, supported_motors, supported_servos, flash_b, psram_b, board_issues,
         active_motors, active_servos, actuator_available, actuators_enabled) = \
            struct.unpack("<BBBBIIIBBBB", info.payload)
        expected_identity = (PROTOCOL_VERSION, 1, 4, 2, 16 * 1024 * 1024, 8 * 1024 * 1024)
        actual_identity = (proto, target, supported_motors, supported_servos, flash_b, psram_b)
        if actual_identity != expected_identity:
            raise RuntimeError("device info mismatch: " + repr(actual_identity))
        fatal_board_mask = (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 14)
        if board_issues & fatal_board_mask:
            raise RuntimeError(f"device info reports fatal board issue mask 0x{board_issues:08x}")
        if (actuators_enabled != 0 or active_motors != 0 or active_servos != 0 or
                actuator_available != 0):
            raise RuntimeError(
                "physical HIL requires actuators-disabled firmware, but DeviceInfo reports "
                f"enabled={actuators_enabled} available={actuator_available} "
                f"active_motors={active_motors} active_servos={active_servos}"
            )
        print("[PASS] runtime ESP32-S3 N16R8 identity/board status + actuators disabled")
        for index in range(3):
            session.send(MessageType.DEVICE_INFO_REQUEST)
            repeated_info = link.wait_for(
                [MessageType.DEVICE_INFO_RESPONSE, MessageType.COMMAND_NACK],
                2.0,
            )
            if (repeated_info.message_type != MessageType.DEVICE_INFO_RESPONSE or
                    repeated_info.payload != info.payload):
                raise RuntimeError(
                    f"repeated device-info request {index + 1} changed/faulted"
                )
        print("[PASS] repeated DeviceInfo requests kept USB CDC stable")

        session.send(MessageType.DEVICE_STATUS_REQUEST)
        status = link.wait_for([MessageType.DEVICE_STATUS_RESPONSE, MessageType.COMMAND_NACK], 2.0)
        if status.message_type != MessageType.DEVICE_STATUS_RESPONSE:
            raise RuntimeError("device-status request failed")
        device_status = parse_device_status(status)
        if device_status.safety_state == SafetyState.FAULT:
            raise RuntimeError("firmware reports FAULT (check N16R8 memory profile and boot log)")
        if (device_status.safety_state != SafetyState.DISARMED or
                device_status.communication_state != CommunicationState.HEALTHY or
                device_status.armed):
            raise RuntimeError(
                "unexpected safe status: "
                f"safety={device_status.safety_state.name} "
                f"comm={device_status.communication_state.name} "
                f"armed={device_status.armed}"
            )
        print("[PASS] safe DISARMED + healthy-link status")
        if device_status.rangefinders is None:
            print("[INFO] legacy DeviceStatus has no per-rangefinder lifecycle evidence")
        else:
            lifecycle_summary = ", ".join(
                f"{RANGEFINDER_ROLES[index]}={lifecycle.name}"
                for index, lifecycle in enumerate(device_status.rangefinders)
            )
            print(f"[PASS] rangefinder lifecycle decoded: {lifecycle_summary}")
        if require_rangefinders:
            if device_status.rangefinders is None:
                raise RuntimeError(
                    "--require-rangefinders requires the extended 10-byte DeviceStatusResponse"
                )
            non_live = {
                RANGEFINDER_ROLES[index]: lifecycle.name
                for index, lifecycle in enumerate(device_status.rangefinders)
                if lifecycle != RangefinderLifecycle.LIVE
            }
            if non_live:
                raise RuntimeError(
                    f"required rangefinders are not all LIVE: {non_live}"
                )
            print("[PASS] all four required rangefinder lifecycle states are LIVE")

        for sensor_id, name in ((1, "SHT30"), (2, "MS5611")):
            req = session.send(MessageType.SET_SENSOR_RATE, struct.pack("<BBI", sensor_id, 0, 100_000))
            session.expect_ack(req)
            print(f"[PASS] {name} runtime sampling-rate command")
        if require_rangefinders:
            for instance_id, role in RANGEFINDER_ROLES.items():
                req = session.send(
                    MessageType.SET_SENSOR_RATE,
                    struct.pack("<BBI", 3, instance_id, 100_000),
                )
                session.expect_ack(req)
                print(f"[PASS] VL53L0X {role} runtime sampling-rate command")

        req = session.send(MessageType.START_TELEMETRY)
        session.expect_ack(req)
        print("[PASS] telemetry start")

        required_sensor_keys = [(1, 0), (2, 0)]
        if require_rangefinders:
            required_sensor_keys.extend((3, instance) for instance in RANGEFINDER_ROLES)
        sample_series: dict[tuple[int, int], list[dict]] = {
            key: [] for key in required_sensor_keys
        }
        deadline = time.monotonic() + sensor_timeout_s
        next_heartbeat = time.monotonic() + 0.35
        while (time.monotonic() < deadline and
               any(len(series) < sensor_sample_count for series in sample_series.values())):
            now = time.monotonic()
            if now >= next_heartbeat:
                session.heartbeat()
                next_heartbeat = time.monotonic() + 0.35
            try:
                frame = link.wait_for([MessageType.SENSOR_SAMPLE], min(0.15, max(0.01, deadline - now)))
            except TimeoutError:
                continue
            sample = parse_sensor_sample(frame.payload)
            sample_key = (sample["sensor_id"], sample["instance_id"])
            if sample_key not in sample_series:
                continue
            description = validate_sensor_sample(sample)
            series = sample_series[sample_key]
            if series:
                validate_sensor_progression(series[-1], sample)
            series.append(sample)
            print(
                f"[PASS] valid {description} seq={sample['sequence']} "
                f"sensor_timestamp_us={sample['timestamp_us']} CRC=PASS"
            )
        missing = {key for key, series in sample_series.items()
                   if len(series) < sensor_sample_count}
        if missing:
            def sensor_name(key: tuple[int, int]) -> str:
                sensor_id, instance_id = key
                if sensor_id == 1:
                    return "SHT30"
                if sensor_id == 2:
                    return "MS5611"
                return f"VL53L0X {RANGEFINDER_ROLES[instance_id]}"
            names = [sensor_name(key) for key in sorted(missing)]
            counts = ", ".join(
                f"{sensor_name(key)}={len(sample_series[key])}/{sensor_sample_count}"
                for key in sorted(missing)
            )
            raise RuntimeError(
                "insufficient valid telemetry from " + ", ".join(names) + f" ({counts}); "
                "check 3.3V/GND, SDA=GPIO8, SCL=GPIO9, addresses 0x44/0x77, "
                "rangefinder XSHUT/address assignment when requested, and pull-ups"
            )

        for key in required_sensor_keys:
            print_sensor_summary(sample_series[key])

        # Both altitude calculations deliberately consume this exact same real
        # MS5611 sample so a QNH/unit regression cannot hide behind sample drift.
        pressure_sample = sample_series[(2, 0)][-1]
        pressure_pa = pressure_sample["fields"][3][1]
        altitude_m = barometric_altitude_m(pressure_pa, qnh_hpa)
        additional_qnh_hpa = 1000.0 if math.isclose(qnh_hpa, 1013.25, abs_tol=0.01) else 1013.25
        additional_altitude_m = barometric_altitude_m(pressure_pa, additional_qnh_hpa)
        print(
            f"[PASS] pressure_sample={pressure_pa} Pa seq={pressure_sample['sequence']} "
            f"barometric_altitude={altitude_m:.2f} m QNH={qnh_hpa:.2f} hPa"
        )
        print(
            f"[PASS] same pressure_sample={pressure_pa} Pa seq={pressure_sample['sequence']} "
            f"barometric_altitude={additional_altitude_m:.2f} m "
            f"additional_QNH={additional_qnh_hpa:.2f} hPa"
        )

        token = struct.pack("<Q", 0x0123456789ABCDEF)
        session.send(MessageType.PING, token)
        pong = link.wait_for([MessageType.PONG, MessageType.COMMAND_NACK], 2.0)
        if pong.message_type != MessageType.PONG or pong.payload != token:
            raise RuntimeError("ping/pong mismatch")
        print("[PASS] ping/pong payload integrity")

        # A bad-CRC frame must not commit its sequence; the following valid frame
        # uses that same sequence and must succeed, demonstrating parser resync.
        bad_seq = session.sequence
        valid = encode_frame(MessageType.PING, bad_seq, token, Priority.NORMAL)
        link.write(corrupt_crc_preserving_cobs(valid))
        time.sleep(0.05)
        link.write(valid)
        session.sequence = (session.sequence + 1) & 0xFFFFFFFF
        pong = link.wait_for([MessageType.PONG, MessageType.COMMAND_NACK], 2.0)
        if pong.message_type != MessageType.PONG or pong.payload != token:
            raise RuntimeError("parser did not recover after corrupt CRC frame")
        print("[PASS] CRC rejection + stream resynchronization")

        req = session.send(MessageType.STOP_TELEMETRY)
        session.expect_ack(req)
        print("[PASS] telemetry stop")

        if reconnect_test:
            ser, port_name, link, session = run_reconnect_session_test(
                ser,
                port_name,
                serial_module,
                session,
                reconnect_timeout_s,
            )
            print(f"[PASS] USB reconnect/session rollover complete on {port_name}")
        print("[PASS] WINDOWS BOARD-LEVEL HIL COMPLETE")
    finally:
        ser.close()


class TeeOutput:
    """Mirror a console stream into an explicitly requested UTF-8 artifact."""

    def __init__(self, *streams: TextIO) -> None:
        self.streams = streams

    def write(self, value: str) -> int:
        for stream in self.streams:
            stream.write(value)
            stream.flush()
        return len(value)

    def flush(self) -> None:
        for stream in self.streams:
            stream.flush()


def execute(args: argparse.Namespace) -> int:
    try:
        self_test()
        if args.self_test:
            return 0
        if args.actuator_test:
            raise RuntimeError(
                "--actuator-test is disabled: this HIL is strictly non-actuating"
            )
        if args.sensor_samples < 2:
            raise ValueError("--sensor-samples must be at least 2")
        if not math.isfinite(args.sensor_timeout) or args.sensor_timeout <= 0:
            raise ValueError("--sensor-timeout must be a positive finite number")
        if args.reconnect_test and (
                not math.isfinite(args.reconnect_timeout) or args.reconnect_timeout <= 0):
            raise ValueError("--reconnect-timeout must be a positive finite number")
        barometric_altitude_m(101325.0, args.qnh_hpa)
        port_name, serial_module = select_port(args.port)
        run_hardware(port_name, serial_module, args.sensor_timeout,
                     args.actuator_test, args.props_removed, args.qnh_hpa,
                     args.sensor_samples, args.reconnect_test,
                     args.reconnect_timeout, args.require_rangefinders)
        return 0
    except (AssertionError, OSError, ProtocolError, RuntimeError, TimeoutError, ValueError) as exc:
        print(f"[FAIL] {exc}", file=sys.stderr)
        return 1


def main(argv: Optional[list[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="Shahbaz ESP32-S3 Windows board-level USB CDC HIL diagnostic")
    parser.add_argument("--self-test", action="store_true", help="run codec tests only; no board needed")
    parser.add_argument("--port", help="Windows COM port for Shahbaz native USB CDC (for example COM7)")
    parser.add_argument("--sensor-timeout", type=float, default=12.0,
                        help="seconds to collect valid samples from every required sensor channel")
    parser.add_argument("--sensor-samples", type=int, default=DEFAULT_SENSOR_SAMPLE_COUNT,
                        help=f"minimum repeated samples per sensor (default {DEFAULT_SENSOR_SAMPLE_COUNT})")
    parser.add_argument("--qnh-hpa", type=float, default=1013.25,
                        help="sea-level pressure used for barometric altitude (default 1013.25 hPa)")
    parser.add_argument("--reconnect-test", action="store_true",
                        help="require an externally triggered real USB detach/re-enumeration "
                             "and test session rollover")
    parser.add_argument("--reconnect-timeout", type=float,
                        default=DEFAULT_RECONNECT_TIMEOUT_S,
                        help=f"seconds to wait for detach + re-enumeration (default {DEFAULT_RECONNECT_TIMEOUT_S:g})")
    parser.add_argument(
        "--require-rangefinders",
        action="store_true",
        help="require valid repeated samples from VL53L0X instances 0..3; use only with the reviewed range profile",
    )
    parser.add_argument("--actuator-test", action="store_true",
                        help="legacy option retained for CLI compatibility; always rejected")
    parser.add_argument("--props-removed", action="store_true",
                        help="legacy acknowledgement; does not enable actuator testing")
    parser.add_argument("--artifact", metavar="PATH",
                        help="also persist the complete UTF-8 HIL/self-test transcript to PATH")
    args = parser.parse_args(argv)

    if not args.artifact:
        return execute(args)

    try:
        artifact_path = Path(args.artifact).expanduser()
        artifact_path.parent.mkdir(parents=True, exist_ok=True)
        with artifact_path.open("w", encoding="utf-8", buffering=1) as artifact:
            with contextlib.redirect_stdout(TeeOutput(sys.stdout, artifact)), \
                    contextlib.redirect_stderr(TeeOutput(sys.stderr, artifact)):
                print(
                    f"[INFO] transcript artifact={artifact_path.resolve()} "
                    f"started_utc={time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())}"
                )
                return execute(args)
    except OSError as exc:
        print(f"[FAIL] cannot create transcript artifact {args.artifact!r}: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
