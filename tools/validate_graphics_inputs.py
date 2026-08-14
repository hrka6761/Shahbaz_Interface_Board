#!/usr/bin/env python3
"""Validate the hardware-evidence inputs used by graphical documentation.

The validator intentionally uses only the Python standard library.  It parses the
restricted YAML subset used by this package, checks evidence provenance and file
hashes, enforces the N16R8 pin restrictions, and rejects unsafe power, sensor,
USB, actuator, or diagram claims.

This is an input-consistency validator.  It cannot replace schematic review,
continuity checks, voltage/current measurements, or physical board identity.
"""
from __future__ import annotations

import csv
import hashlib
import re
import struct
import sys
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
HARDWARE_INPUTS = ROOT / "hardware_reference"
MANIFESTS = HARDWARE_INPUTS / "04_project_hardware_configuration"
DIAGRAM_SOURCES = ROOT / "hardware_reference" / "05_project_hardware_diagrams"

ALLOWED_STATUSES = {
    "VERIFIED_MEASUREMENT",
    "USER_PHOTO",
    "OFFICIAL_DATASHEET",
    "PROJECT_CONFIGURATION",
    "CANDIDATE_REFERENCE",
    "UNVERIFIED",
}

ERRORS: list[str] = []
WARNINGS: list[str] = []


def error(message: str) -> None:
    ERRORS.append(message)


def warn(message: str) -> None:
    WARNINGS.append(message)


def relative(path: Path) -> str:
    try:
        return path.relative_to(ROOT).as_posix()
    except ValueError:
        return str(path)


def require_file(path: Path) -> None:
    if not path.is_file():
        error(f"Missing required file: {relative(path)}")


class YamlSubsetError(ValueError):
    """Raised when an input uses YAML outside the deliberately small subset."""


def _strip_yaml_comment(line: str) -> str:
    quote: str | None = None
    escaped = False
    for index, char in enumerate(line):
        if escaped:
            escaped = False
            continue
        if char == "\\" and quote == '"':
            escaped = True
            continue
        if char in {"'", '"'}:
            if quote is None:
                quote = char
            elif quote == char:
                quote = None
            continue
        if char == "#" and quote is None and (index == 0 or line[index - 1].isspace()):
            return line[:index].rstrip()
    return line.rstrip()


def _split_inline_list(value: str, source: Path, line_number: int) -> list[Any]:
    inner = value[1:-1].strip()
    if not inner:
        return []
    try:
        entries = next(csv.reader([inner], skipinitialspace=True))
    except csv.Error as exc:
        raise YamlSubsetError(f"{relative(source)}:{line_number}: invalid inline list: {exc}") from exc
    return [_parse_yaml_scalar(entry.strip(), source, line_number) for entry in entries]


def _parse_yaml_scalar(value: str, source: Path, line_number: int) -> Any:
    value = value.strip()
    if value == "":
        raise YamlSubsetError(f"{relative(source)}:{line_number}: missing scalar value")
    if value.startswith("["):
        if not value.endswith("]"):
            raise YamlSubsetError(f"{relative(source)}:{line_number}: unterminated inline list")
        return _split_inline_list(value, source, line_number)
    if (value.startswith("'") and value.endswith("'")) or (
        value.startswith('"') and value.endswith('"')
    ):
        return value[1:-1]
    lowered = value.lower()
    if lowered in {"null", "~"}:
        return None
    if lowered == "true":
        return True
    if lowered == "false":
        return False
    if re.fullmatch(r"[-+]?\d+", value):
        return int(value)
    if re.fullmatch(r"[-+]?(?:\d+\.\d*|\d*\.\d+)(?:[eE][-+]?\d+)?", value):
        return float(value)
    return value


def _split_yaml_mapping(text: str, source: Path, line_number: int) -> tuple[str, str]:
    if ":" not in text:
        raise YamlSubsetError(f"{relative(source)}:{line_number}: expected key: value")
    key, value = text.split(":", 1)
    key = key.strip()
    if not key or any(char in key for char in "{}[]"):
        raise YamlSubsetError(f"{relative(source)}:{line_number}: invalid mapping key {key!r}")
    return key, value.strip()


def load_yaml_subset(path: Path) -> Any:
    """Parse mappings, sequences, scalars, and inline scalar lists.

    Anchors, aliases, tags, block scalars, flow mappings, and multiline scalars
    are rejected.  The package manifests intentionally do not need them.
    """

    tokens: list[tuple[int, int, str]] = []
    for line_number, raw_line in enumerate(path.read_text(encoding="utf-8-sig").splitlines(), 1):
        if "\t" in raw_line[: len(raw_line) - len(raw_line.lstrip())]:
            raise YamlSubsetError(f"{relative(path)}:{line_number}: tabs are not allowed for indentation")
        without_comment = _strip_yaml_comment(raw_line)
        if not without_comment.strip() or without_comment.lstrip().startswith("---"):
            continue
        indent = len(without_comment) - len(without_comment.lstrip(" "))
        if indent % 2:
            raise YamlSubsetError(f"{relative(path)}:{line_number}: indentation must use multiples of two spaces")
        tokens.append((line_number, indent, without_comment.strip()))

    if not tokens:
        raise YamlSubsetError(f"{relative(path)}: empty YAML document")

    def parse_block(position: int, indent: int) -> tuple[Any, int]:
        if position >= len(tokens) or tokens[position][1] != indent:
            line_number = tokens[position][0] if position < len(tokens) else tokens[-1][0]
            raise YamlSubsetError(f"{relative(path)}:{line_number}: invalid indentation")

        is_sequence = tokens[position][2].startswith("- ") or tokens[position][2] == "-"
        container: Any = [] if is_sequence else {}

        while position < len(tokens):
            line_number, current_indent, text = tokens[position]
            if current_indent < indent:
                break
            if current_indent > indent:
                raise YamlSubsetError(f"{relative(path)}:{line_number}: unexpected indentation")

            current_is_sequence = text.startswith("- ") or text == "-"
            if current_is_sequence != is_sequence:
                raise YamlSubsetError(f"{relative(path)}:{line_number}: mixed mapping and sequence entries")

            if is_sequence:
                item_text = text[1:].strip()
                position += 1
                if not item_text:
                    if position >= len(tokens) or tokens[position][1] <= indent:
                        raise YamlSubsetError(f"{relative(path)}:{line_number}: empty sequence item")
                    item, position = parse_block(position, tokens[position][1])
                    container.append(item)
                    continue

                if ":" not in item_text:
                    container.append(_parse_yaml_scalar(item_text, path, line_number))
                    continue

                key, raw_value = _split_yaml_mapping(item_text, path, line_number)
                item_map: dict[str, Any] = {}
                if raw_value:
                    item_map[key] = _parse_yaml_scalar(raw_value, path, line_number)
                elif position < len(tokens) and tokens[position][1] > indent:
                    item_map[key], position = parse_block(position, tokens[position][1])
                else:
                    item_map[key] = {}

                if position < len(tokens) and tokens[position][1] > indent:
                    extra_indent = tokens[position][1]
                    extra, position = parse_block(position, extra_indent)
                    if not isinstance(extra, dict):
                        raise YamlSubsetError(
                            f"{relative(path)}:{tokens[position - 1][0]}: sequence item properties must be a mapping"
                        )
                    duplicate = set(item_map) & set(extra)
                    if duplicate:
                        raise YamlSubsetError(
                            f"{relative(path)}:{line_number}: duplicate key(s) {sorted(duplicate)}"
                        )
                    item_map.update(extra)
                container.append(item_map)
                continue

            key, raw_value = _split_yaml_mapping(text, path, line_number)
            if key in container:
                raise YamlSubsetError(f"{relative(path)}:{line_number}: duplicate key {key!r}")
            position += 1
            if raw_value:
                container[key] = _parse_yaml_scalar(raw_value, path, line_number)
            elif position < len(tokens) and tokens[position][1] > indent:
                container[key], position = parse_block(position, tokens[position][1])
            else:
                container[key] = {}

        return container, position

    result, final_position = parse_block(0, tokens[0][1])
    if final_position != len(tokens):
        line_number = tokens[final_position][0]
        raise YamlSubsetError(f"{relative(path)}:{line_number}: unparsed YAML content")
    return result


def validate_status_values(value: Any, location: str) -> None:
    if isinstance(value, dict):
        for key, child in value.items():
            child_location = f"{location}.{key}"
            if key == "status" or key.endswith("_status"):
                if child not in ALLOWED_STATUSES:
                    error(f"Invalid evidence status at {child_location}: {child!r}")
            validate_status_values(child, child_location)
    elif isinstance(value, list):
        for index, child in enumerate(value):
            validate_status_values(child, f"{location}[{index}]")


def get_path(mapping: Any, dotted_path: str, default: Any = None) -> Any:
    current = mapping
    for part in dotted_path.split("."):
        if not isinstance(current, dict) or part not in current:
            return default
        current = current[part]
    return current


def has_path(mapping: Any, dotted_path: str) -> bool:
    current = mapping
    for part in dotted_path.split("."):
        if not isinstance(current, dict) or part not in current:
            return False
        current = current[part]
    return True


def expect_path(mapping: Any, dotted_path: str, expected: Any, source_name: str) -> None:
    actual = get_path(mapping, dotted_path)
    if actual != expected:
        error(f"{source_name}.{dotted_path} must be {expected!r}, found {actual!r}")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def ensure_within_root(path: Path, description: str) -> bool:
    try:
        path.resolve().relative_to(ROOT.resolve())
    except ValueError:
        error(f"{description} escapes the package root: {path}")
        return False
    return True


def image_dimensions(path: Path) -> tuple[int, int]:
    with path.open("rb") as handle:
        signature = handle.read(24)
        if signature.startswith(b"\x89PNG\r\n\x1a\n"):
            if len(signature) < 24 or signature[12:16] != b"IHDR":
                raise ValueError("invalid PNG IHDR")
            width, height = struct.unpack(">II", signature[16:24])
            return width, height

        handle.seek(0)
        if handle.read(2) != b"\xff\xd8":
            raise ValueError("unsupported or invalid image signature")
        sof_markers = {
            0xC0,
            0xC1,
            0xC2,
            0xC3,
            0xC5,
            0xC6,
            0xC7,
            0xC9,
            0xCA,
            0xCB,
            0xCD,
            0xCE,
            0xCF,
        }
        while True:
            byte = handle.read(1)
            if not byte:
                raise ValueError("JPEG has no start-of-frame marker")
            if byte != b"\xff":
                continue
            marker_byte = handle.read(1)
            while marker_byte == b"\xff":
                marker_byte = handle.read(1)
            if not marker_byte:
                raise ValueError("truncated JPEG marker")
            marker = marker_byte[0]
            if marker in {0x01, *range(0xD0, 0xD9)}:
                continue
            length_bytes = handle.read(2)
            if len(length_bytes) != 2:
                raise ValueError("truncated JPEG segment length")
            segment_length = struct.unpack(">H", length_bytes)[0]
            if segment_length < 2:
                raise ValueError("invalid JPEG segment length")
            payload = handle.read(segment_length - 2)
            if len(payload) != segment_length - 2:
                raise ValueError("truncated JPEG segment")
            if marker in sof_markers:
                if len(payload) < 5:
                    raise ValueError("truncated JPEG start-of-frame segment")
                height, width = struct.unpack(">HH", payload[1:5])
                return width, height


def validate_official_hashes() -> None:
    hash_file = HARDWARE_INPUTS / "99_source_integrity" / "official_files_sha256.txt"
    require_file(hash_file)
    if not hash_file.is_file():
        return
    seen_paths: set[str] = set()
    for line_number, line in enumerate(hash_file.read_text(encoding="utf-8-sig").splitlines(), 1):
        if not line.strip():
            continue
        match = re.fullmatch(r"([0-9a-fA-F]{64})\s{2,}(.+)", line.strip())
        if not match:
            error(f"{relative(hash_file)}:{line_number}: invalid SHA-256 record")
            continue
        expected_hash, relative_name = match.groups()
        if relative_name in seen_paths:
            error(f"Duplicate official-datasheet hash path: {relative_name}")
            continue
        seen_paths.add(relative_name)
        target = ROOT / Path(relative_name)
        if not ensure_within_root(target, "Official datasheet hash path"):
            continue
        if not target.is_file():
            error(f"Hashed official datasheet is missing: {relative_name}")
            continue
        actual_hash = sha256_file(target)
        if actual_hash.lower() != expected_hash.lower():
            error(f"Official datasheet hash mismatch: {relative_name}")

    if len(seen_paths) != 8:
        error(f"Expected 8 official manufacturer-file hash records, found {len(seen_paths)}")


def validate_hardware_manifest(hardware: Any) -> None:
    if not isinstance(hardware, dict):
        error("project_hardware_profile.yaml root must be a mapping")
        return
    declared_value = get_path(hardware, "verification_policy.allowed_statuses", [])
    declared = set(declared_value) if isinstance(declared_value, list) else set()
    if not isinstance(declared_value, list) or declared != ALLOWED_STATUSES:
        error("project_hardware_profile.yaml allowed_statuses must exactly match the specification vocabulary")
    validate_status_values(hardware, "hardware_manifest")

    expectations = {
        "schema_version": 4,
        "board.module_profile": "N16R8",
        "board.module_profile_status": "PROJECT_CONFIGURATION",
        "board.flash_mb": 16,
        "board.psram_mb": 8,
        "board.gpio_logic_v": 3.3,
        "board.usb_ports.native_usb_otg.d_minus_gpio": 19,
        "board.usb_ports.native_usb_otg.d_plus_gpio": 20,
        "board.i2c.sda_gpio": 8,
        "board.i2c.scl_gpio": 9,
        "board.i2c.voltage_v": 3.3,
        "board.onboard_resources.rgb_led.reference_board_gpio": 48,
        "board.onboard_resources.rgb_led.reference_board_gpio_status": "CANDIDATE_REFERENCE",
        "power.source_type": "regulated_5v_battery",
        "power.sole_operating_power_source": True,
        "power.sensors_powered_from_board_3v3": True,
        "power.direct_5v_to_sensor_vcc_allowed": False,
        "power.android_is_usb_host": True,
        "power.android_usb_data_only": True,
        "power.android_vbus_must_not_power_board_or_sensors": True,
        "power.android_vbus_connection_to_battery_rail_allowed": False,
        "power.vbus_current_blocking_required": True,
        "power.common_usb_signal_ground_required": True,
        "safety.phase_state": "DISARMED",
        "safety.arming_supported": False,
        "safety.actuator_commands_accepted": False,
        "safety.physical_waveform_outputs_allowed": False,
        "actuators.controller": "NullActuatorController",
        "actuators.physical_outputs_enabled": False,
        "actuators.output_peripherals_initialized": False,
    }
    for dotted_path, expected in expectations.items():
        expect_path(hardware, dotted_path, expected, "hardware_manifest")
    for dotted_path in (
        "board.board_identity_evidence_record_id",
        "board.usb_ports.native_usb_otg.exact_board_trace_path",
        "board.usb_ports.native_usb_otg.evidence_record_id",
        "board.i2c.evidence_record_id",
        "board.onboard_resources.rgb_led.evidence_record_id",
        "power.usb_evidence_record_id",
    ):
        if not has_path(hardware, dotted_path):
            error(f"hardware_manifest.{dotted_path} is required by evidence schema version 4")

    unavailable_value = get_path(hardware, "board.unavailable_module_gpios", [])
    unavailable = set(unavailable_value) if isinstance(unavailable_value, list) else set()
    if not isinstance(unavailable_value, list) or unavailable != {35, 36, 37}:
        error(f"N16R8 unavailable_module_gpios must be [35, 36, 37], found {sorted(unavailable)}")
    nonexistent_value = get_path(hardware, "board.nonexistent_soc_gpios", [])
    nonexistent = set(nonexistent_value) if isinstance(nonexistent_value, list) else set()
    if not isinstance(nonexistent_value, list) or nonexistent != {22, 23, 24, 25}:
        error(f"ESP32-S3 nonexistent_soc_gpios must be [22, 23, 24, 25], found {sorted(nonexistent)}")
    memory_path_value = get_path(hardware, "board.internal_memory_path_gpios", [])
    memory_path = set(memory_path_value) if isinstance(memory_path_value, list) else set()
    if not isinstance(memory_path_value, list) or memory_path != set(range(26, 35)):
        error(f"WROOM internal_memory_path_gpios must be GPIO26-GPIO34, found {sorted(memory_path)}")

    sensors = get_path(hardware, "sensors", [])
    if not isinstance(sensors, list):
        error("hardware_manifest.sensors must be a sequence")
        return
    sensor_map = {entry.get("id"): entry for entry in sensors if isinstance(entry, dict)}
    if len(sensor_map) != len(sensors):
        error("hardware_manifest sensor IDs must be present and unique")
    sht = sensor_map.get("sht30_ambient", {})
    ms = sensor_map.get("ms5611_baro", {})
    for name, sensor in (("sht30_ambient", sht), ("ms5611_baro", ms)):
        if not sensor:
            error(f"hardware_manifest is missing sensor {name}")
            continue
        if "evidence_record_id" not in sensor:
            error(f"hardware_manifest sensor {name} requires evidence_record_id")
        if sensor.get("configured_supply_v") != 3.3:
            error(f"{name} configured supply must be 3.3 V")
    if sht:
        if set(sht.get("supported_addresses_7bit", [])) != {"0x44", "0x45"}:
            error("SHT3x supported address set must be 0x44/0x45")
    if ms:
        for key, expected in {
            "configured_ps_state": "HIGH",
            "configured_csb_state": "LOW",
            "configured_sdo_state": "NC",
        }.items():
            if ms.get(key) != expected:
                error(f"ms5611_baro.{key} must be {expected}")
def validate_measurements(measurements: Any) -> None:
    if not isinstance(measurements, dict):
        error("hardware_measurements.yaml root must be a mapping")
        return
    validate_status_values(measurements, "electrical_measurements")
    if measurements.get("schema_version") != 3:
        error("hardware_measurements.yaml schema_version must be 3")
    if not has_path(measurements, "measurement_metadata.evidence_record_id"):
        error("electrical_measurements.measurement_metadata.evidence_record_id is required")
    for group_name in ("measurement_metadata", "sht30", "gy63_ms5611", "shared_i2c_bus", "power_path"):
        group = measurements.get(group_name)
        if not isinstance(group, dict):
            error(f"electrical_measurements.{group_name} must be a mapping")
            continue
        status = group.get("status")
        values = {key: value for key, value in group.items() if key != "status"}
        if status == "UNVERIFIED":
            populated = [key for key, value in values.items() if value is not None]
            if populated:
                error(
                    f"electrical_measurements.{group_name} is UNVERIFIED but contains non-null result(s): "
                    + ", ".join(sorted(populated))
                )
        elif status == "VERIFIED_MEASUREMENT":
            if group_name == "measurement_metadata":
                required_metadata = (
                    "evidence_record_id",
                    "measured_at_utc",
                    "operator",
                    "instrument_make_model",
                    "instrument_calibration_state",
                )
                missing = [key for key in required_metadata if not is_concrete(values.get(key))]
                if missing:
                    error(
                        "electrical_measurements.measurement_metadata is VERIFIED_MEASUREMENT "
                        "but lacks concrete field(s): " + ", ".join(missing)
                    )
            elif not any(is_concrete(value) for value in values.values()):
                error(f"electrical_measurements.{group_name} is VERIFIED_MEASUREMENT but has no results")
        else:
            error(f"electrical_measurements.{group_name}.status must be UNVERIFIED or VERIFIED_MEASUREMENT")

    metadata = measurements.get("measurement_metadata", {})
    metadata_has_record = (
        isinstance(metadata, dict)
        and metadata.get("status") == "VERIFIED_MEASUREMENT"
        and is_concrete(metadata.get("evidence_record_id"))
    )
    for group_name in ("sht30", "gy63_ms5611", "shared_i2c_bus", "power_path"):
        group = measurements.get(group_name, {})
        if isinstance(group, dict) and group.get("status") == "VERIFIED_MEASUREMENT" and not metadata_has_record:
            error(
                f"electrical_measurements.{group_name} requires verified metadata "
                "with a nonempty evidence_record_id"
            )

    requirements = measurements.get("project_requirements")
    if not isinstance(requirements, dict):
        error("electrical_measurements.project_requirements must be a mapping")
        return
    expected = {
        "configured_source": "regulated_5v_battery",
        "nominal_source_voltage_v": 5.0,
        "usb_vbus_continuity_to_board_power_requirement": "OPEN_OR_CURRENT_BLOCKED",
        "android_vbus_power_source_allowed": False,
        "common_usb_signal_ground_required": True,
        "status": "PROJECT_CONFIGURATION",
    }
    for key, value in expected.items():
        if requirements.get(key) != value:
            error(f"electrical_measurements.project_requirements.{key} must be {value!r}")


def is_concrete(value: Any) -> bool:
    return value is not None and (not isinstance(value, str) or bool(value.strip()))


def validate_cross_manifest_evidence(hardware: Any, measurements: Any) -> None:
    """Require every promoted physical claim to point at concrete evidence.

    A VERIFIED_MEASUREMENT claim must use the same nonempty record identifier as
    verified measurement metadata and must have the claim-specific observations.
    USER_PHOTO is accepted only for identity fields with a nonempty record ID;
    electrical routing and power claims always require measurements.
    """
    if not isinstance(hardware, dict) or not isinstance(measurements, dict):
        return

    metadata = measurements.get("measurement_metadata", {})
    metadata_verified = isinstance(metadata, dict) and metadata.get("status") == "VERIFIED_MEASUREMENT"
    measurement_record_id = metadata.get("evidence_record_id") if isinstance(metadata, dict) else None

    def require_record(record_id: Any, label: str, require_measurement_record: bool) -> None:
        if not is_concrete(record_id):
            error(f"{label} requires a nonempty evidence_record_id")
        if require_measurement_record:
            if not metadata_verified:
                error(f"{label} requires VERIFIED_MEASUREMENT metadata")
            elif record_id != measurement_record_id:
                error(f"{label} evidence_record_id must match measurement_metadata.evidence_record_id")

    def require_group(group_name: str, required_fields: tuple[str, ...], label: str) -> None:
        group = measurements.get(group_name, {})
        if not isinstance(group, dict) or group.get("status") != "VERIFIED_MEASUREMENT":
            error(f"{label} requires electrical_measurements.{group_name}.status VERIFIED_MEASUREMENT")
            return
        missing = [field for field in required_fields if not is_concrete(group.get(field))]
        if missing:
            error(f"{label} lacks concrete measurement field(s): " + ", ".join(missing))

    def value_transition(
        container: Any,
        value_key: str,
        status_key: str,
        record_id: Any,
        label: str,
        allowed_verified: tuple[str, ...] = ("VERIFIED_MEASUREMENT",),
    ) -> str:
        if not isinstance(container, dict):
            error(f"{label} container is missing")
            return "INVALID"
        status = container.get(status_key)
        allowed = {"UNVERIFIED", *allowed_verified}
        if status not in allowed:
            error(f"{label} status must be one of {sorted(allowed)}, found {status!r}")
            return "INVALID"
        value = container.get(value_key)
        if status == "UNVERIFIED":
            if value is not None:
                error(f"{label} is UNVERIFIED but {value_key} is not null")
        else:
            if not is_concrete(value):
                error(f"{label} is {status} but {value_key} is not concrete")
            require_record(record_id, label, status == "VERIFIED_MEASUREMENT")
        return str(status)

    board = hardware.get("board", {})
    if isinstance(board, dict):
        board_record = board.get("board_identity_evidence_record_id")
        for value_key, status_key, label in (
            ("exact_manufacturer", "exact_manufacturer_status", "Exact board manufacturer"),
            ("pcb_revision", "pcb_revision_status", "Exact PCB revision"),
            ("exact_module_marking", "exact_module_marking_status", "Exact module marking"),
        ):
            value_transition(
                board,
                value_key,
                status_key,
                board_record,
                label,
                ("USER_PHOTO", "VERIFIED_MEASUREMENT"),
            )

        usb = get_path(board, "usb_ports.native_usb_otg", {})
        usb_record = usb.get("evidence_record_id") if isinstance(usb, dict) else None
        connector_status = value_transition(
            usb,
            "connector_identity",
            "connector_identity_status",
            usb_record,
            "Native USB connector identity",
            ("USER_PHOTO", "VERIFIED_MEASUREMENT"),
        )
        route_status = value_transition(
            usb,
            "exact_board_trace_path",
            "exact_board_trace_path_status",
            usb_record,
            "Native USB exact-board trace path",
        )
        if connector_status == "VERIFIED_MEASUREMENT" or route_status == "VERIFIED_MEASUREMENT":
            require_group(
                "power_path",
                (
                    "native_usb_connector_identity",
                    "usb_d_minus_continuity_to_gpio19_verified",
                    "usb_d_plus_continuity_to_gpio20_verified",
                ),
                "Native USB route verification",
            )
            power_path = measurements.get("power_path", {})
            if isinstance(power_path, dict):
                for key in ("usb_d_minus_continuity_to_gpio19_verified", "usb_d_plus_continuity_to_gpio20_verified"):
                    if power_path.get(key) is not True:
                        error(f"Native USB route verification requires {key}: true")

        i2c = board.get("i2c", {})
        if isinstance(i2c, dict):
            i2c_status = value_transition(
                i2c,
                "target_frequency_electrically_verified",
                "target_frequency_electrical_status",
                i2c.get("evidence_record_id"),
                "Shared I2C target frequency",
            )
            if i2c_status == "VERIFIED_MEASUREMENT":
                if i2c.get("target_frequency_electrically_verified") is not True:
                    error("Shared I2C target-frequency verification value must be true")
                require_group("shared_i2c_bus", ("rise_time_ns_at_400khz", "fall_time_ns_at_400khz"),
                              "Shared I2C target frequency")

        rgb = get_path(board, "onboard_resources.rgb_led", {})
        if isinstance(rgb, dict):
            rgb_record = rgb.get("evidence_record_id")
            applicability = rgb.get("applicability_to_current_board_status")
            selected_status = value_transition(
                rgb,
                "selected_gpio",
                "selected_gpio_status",
                rgb_record,
                "Selected onboard RGB GPIO",
                ("USER_PHOTO", "VERIFIED_MEASUREMENT"),
            )
            if applicability not in {"UNVERIFIED", "USER_PHOTO", "VERIFIED_MEASUREMENT"}:
                error("RGB mapping applicability status has an invalid physical-evidence transition")
            elif applicability != "UNVERIFIED":
                require_record(rgb_record, "RGB mapping applicability", applicability == "VERIFIED_MEASUREMENT")
                if rgb.get("selected_gpio") not in {38, 48}:
                    error("Verified RGB mapping applicability requires selected_gpio 38 or 48")
            elif selected_status != "UNVERIFIED":
                error("A selected RGB GPIO cannot be verified while applicability remains UNVERIFIED")

    sensor_groups = {
        "sht30_ambient": ("sht30", "exact_installed_sensor_variant", "exact_installed_sensor_variant_status"),
        "ms5611_baro": ("gy63_ms5611", "exact_sensor_marking", "exact_sensor_marking_status"),
    }
    sensors = hardware.get("sensors", [])
    if isinstance(sensors, list):
        for sensor in sensors:
            if not isinstance(sensor, dict) or sensor.get("id") not in sensor_groups:
                continue
            group_name, identity_key, identity_status_key = sensor_groups[sensor["id"]]
            label = str(sensor["id"])
            record_id = sensor.get("evidence_record_id")
            identity_status = value_transition(
                sensor,
                identity_key,
                identity_status_key,
                record_id,
                f"{label} exact identity",
                ("USER_PHOTO", "VERIFIED_MEASUREMENT"),
            )
            address_status = value_transition(
                sensor,
                "actual_address_7bit",
                "actual_address_status",
                record_id,
                f"{label} actual I2C address",
            )
            overall_status = sensor.get("status")
            if overall_status not in {"UNVERIFIED", "VERIFIED_MEASUREMENT"}:
                error(f"{label} overall status must be UNVERIFIED or VERIFIED_MEASUREMENT")
            elif overall_status == "VERIFIED_MEASUREMENT":
                require_record(record_id, f"{label} hardware verification", True)
                if identity_status not in {"USER_PHOTO", "VERIFIED_MEASUREMENT"}:
                    error(f"{label} hardware verification requires an evidence-backed exact identity")
                if address_status != "VERIFIED_MEASUREMENT":
                    error(f"{label} hardware verification requires a measured I2C address")
                required = ("supply_voltage_v", "actual_i2c_address_7bit")
                if sensor["id"] == "ms5611_baro":
                    required += ("prom_crc_passed_on_actual_hardware",)
                require_group(group_name, required, f"{label} hardware verification")

    power = hardware.get("power", {})
    if isinstance(power, dict):
        record_id = power.get("usb_evidence_record_id")
        power_requirements = {
            "battery_output_under_load_status": ("measured_battery_voltage_under_load_v",),
            "exact_board_5v_input_path_status": (
                "measured_battery_voltage_under_load_v",
                "verified_board_5v_or_vin_input_point",
                "verified_input_polarity",
                "sensor_3v3_rail_measured_v",
            ),
            "exact_vbus_isolation_implementation_status": (
                "usb_vbus_continuity_to_board_5v_rail_ohm",
                "usb_vbus_isolation_method",
                "usb_vbus_isolation_verified",
                "common_usb_signal_ground_verified",
            ),
            "usb_enumeration_with_vbus_isolation_status": (
                "usb_enumeration_with_vbus_blocked_verified",
            ),
            "reverse_current_status": (
                "reverse_current_from_battery_to_phone_ma",
                "reverse_current_from_phone_to_battery_or_board_ma",
            ),
        }
        power_path = measurements.get("power_path", {})
        for status_key, required_fields in power_requirements.items():
            status = power.get(status_key)
            if status not in {"UNVERIFIED", "VERIFIED_MEASUREMENT"}:
                error(f"power.{status_key} must be UNVERIFIED or VERIFIED_MEASUREMENT")
                continue
            if status == "VERIFIED_MEASUREMENT":
                require_record(record_id, f"power.{status_key}", True)
                require_group("power_path", required_fields, f"power.{status_key}")
                if isinstance(power_path, dict):
                    for boolean_key in (
                        "usb_vbus_isolation_verified",
                        "common_usb_signal_ground_verified",
                        "usb_enumeration_with_vbus_blocked_verified",
                    ):
                        if boolean_key in required_fields and power_path.get(boolean_key) is not True:
                            error(f"power.{status_key} requires {boolean_key}: true")


def validate_photos(photo_manifest: Any) -> None:
    if not isinstance(photo_manifest, dict):
        error("hardware_photos.yaml root must be a mapping")
        return
    validate_status_values(photo_manifest, "photo_manifest")
    photos = photo_manifest.get("photos")
    if not isinstance(photos, list):
        error("photo_manifest.photos must be a sequence")
        return
    required_ids = {
        "current_board_front",
        "current_board_back",
        "official_yd_board_front",
        "official_yd_hardware_overview",
        "sht30_front",
        "sht30_back",
        "gy63_front",
        "gy63_back",
    }
    ids = [entry.get("id") for entry in photos if isinstance(entry, dict)]
    if len(ids) != len(set(ids)):
        error("Duplicate photo IDs exist in hardware_photos.yaml")
    missing_ids = required_ids - set(ids)
    if missing_ids:
        error(f"hardware_photos.yaml is missing required IDs: {sorted(missing_ids)}")

    for index, photo in enumerate(photos):
        if not isinstance(photo, dict):
            error(f"photo_manifest.photos[{index}] must be a mapping")
            continue
        photo_id = photo.get("id", f"index {index}")
        raw_path = photo.get("path")
        if not isinstance(raw_path, str):
            error(f"Photo {photo_id} has no valid path")
            continue
        path = (MANIFESTS / raw_path).resolve()
        if not ensure_within_root(path, f"Photo {photo_id} path"):
            continue
        present = photo.get("present")
        if not isinstance(present, bool):
            error(f"Photo {photo_id} present must be true or false")
            continue
        if not photo.get("label_orientation"):
            error(f"Photo {photo_id} must record label_orientation")
        resolution = photo.get("resolution_px")
        if not isinstance(resolution, dict) or set(resolution) != {"width", "height"}:
            error(f"Photo {photo_id} must record resolution_px width and height")
            resolution = {"width": None, "height": None}

        if not present:
            if path.exists():
                error(f"Photo {photo_id} is marked absent but exists: {relative(path)}")
            if photo.get("status") != "UNVERIFIED":
                error(f"Missing photo {photo_id} must have status UNVERIFIED")
            if photo.get("sha256") is not None:
                error(f"Missing photo {photo_id} must have a null sha256")
            if resolution.get("width") is not None or resolution.get("height") is not None:
                error(f"Missing photo {photo_id} must have null resolution dimensions")
            warn(f"Exact image missing; physical overlay is blocked: {relative(path)}")
            continue

        if not path.is_file():
            error(f"Photo {photo_id} is marked present but missing: {relative(path)}")
            continue
        if photo.get("status") not in {"USER_PHOTO", "CANDIDATE_REFERENCE"}:
            error(f"Present photo {photo_id} must use USER_PHOTO or CANDIDATE_REFERENCE status")
        expected_hash = photo.get("sha256")
        if not isinstance(expected_hash, str) or not re.fullmatch(r"[0-9a-f]{64}", expected_hash):
            error(f"Photo {photo_id} must provide a lowercase SHA-256 digest")
        elif sha256_file(path) != expected_hash:
            error(f"Photo hash mismatch: {relative(path)}")
        try:
            actual_width, actual_height = image_dimensions(path)
        except (OSError, ValueError) as exc:
            error(f"Invalid image {relative(path)}: {exc}")
        else:
            if (resolution.get("width"), resolution.get("height")) != (actual_width, actual_height):
                error(
                    f"Photo {photo_id} resolution metadata is "
                    f"{resolution.get('width')}x{resolution.get('height')}, actual is {actual_width}x{actual_height}"
                )

        original_name = photo.get("source_original")
        if original_name is not None:
            original = (MANIFESTS / str(original_name)).resolve()
            if not ensure_within_root(original, f"Photo {photo_id} source_original"):
                continue
            if not original.is_file():
                error(f"Photo source original is missing: {relative(original)}")
                continue
            original_hash = photo.get("source_original_sha256")
            if not isinstance(original_hash, str) or not re.fullmatch(r"[0-9a-f]{64}", original_hash):
                error(f"Photo {photo_id} must provide source_original_sha256")
            elif sha256_file(original) != original_hash:
                error(f"Photo source-original hash mismatch: {relative(original)}")


def read_csv_rows(path: Path, required_columns: set[str]) -> list[dict[str, str]]:
    if not path.is_file():
        return []
    try:
        with path.open(newline="", encoding="utf-8-sig") as handle:
            reader = csv.DictReader(handle)
            actual_columns = set(reader.fieldnames or [])
            missing = required_columns - actual_columns
            if missing:
                error(f"{relative(path)} is missing columns: {sorted(missing)}")
            rows = list(reader)
    except (OSError, csv.Error) as exc:
        error(f"Unable to parse {relative(path)}: {exc}")
        return []
    for line_number, row in enumerate(rows, 2):
        if None in row:
            error(f"{relative(path)}:{line_number}: row has more values than headers")
    return rows


def gpio_references(row: dict[str, str]) -> set[int]:
    references: set[int] = set()
    for field in ("source_gpio_or_net", "destination_pin_label"):
        for match in re.finditer(r"\bGPIO(\d+)\b", str(row.get(field, "") or ""), re.IGNORECASE):
            references.add(int(match.group(1)))
    return references


def validate_csv_manifests() -> tuple[list[dict[str, str]], dict[int, dict[str, str]]]:
    connections_path = MANIFESTS / "project_wiring.csv"
    pins_path = MANIFESTS / "gpio_usage_rules.csv"
    connection_columns = {
        "connection_id",
        "source_component",
        "source_pin_label",
        "source_gpio_or_net",
        "destination_component",
        "destination_pin_label",
        "signal_name",
        "voltage_domain",
        "direction",
        "requirement",
        "verification_status",
        "notes",
    }
    pin_columns = {"gpio", "classification", "owner_or_reason", "rule", "verification_status"}
    connections = read_csv_rows(connections_path, connection_columns)
    pin_rows = read_csv_rows(pins_path, pin_columns)

    ids = [row.get("connection_id", "") for row in connections]
    if any(not connection_id for connection_id in ids):
        error("Every wiring row must have a connection_id")
    if len(ids) != len(set(ids)):
        error("Duplicate connection_id values exist in project_wiring.csv")

    pins: dict[int, dict[str, str]] = {}
    allowed_classes = {
        "ASSIGNED",
        "RESERVED",
        "REVISION_RESERVED",
        "SENSITIVE",
        "AVAILABLE_WITH_REVIEW",
        "FORBIDDEN",
        "NONEXISTENT",
    }
    for line_number, row in enumerate(pin_rows, 2):
        try:
            gpio = int(row.get("gpio", ""))
        except (TypeError, ValueError):
            error(f"{relative(pins_path)}:{line_number}: GPIO must be an integer")
            continue
        if gpio in pins:
            error(f"Duplicate GPIO {gpio} in gpio_usage_rules.csv")
        pins[gpio] = row
        if row.get("classification") not in allowed_classes:
            error(f"GPIO{gpio} has invalid classification {row.get('classification')!r}")
        if row.get("verification_status") not in ALLOWED_STATUSES:
            error(f"GPIO{gpio} has invalid verification_status {row.get('verification_status')!r}")

    expected_pin_classes = {
        0: "SENSITIVE",
        3: "SENSITIVE",
        8: "ASSIGNED",
        9: "ASSIGNED",
        19: "RESERVED",
        20: "RESERVED",
        22: "NONEXISTENT",
        23: "NONEXISTENT",
        24: "NONEXISTENT",
        25: "NONEXISTENT",
        26: "FORBIDDEN",
        27: "FORBIDDEN",
        28: "FORBIDDEN",
        29: "FORBIDDEN",
        30: "FORBIDDEN",
        31: "FORBIDDEN",
        32: "FORBIDDEN",
        33: "FORBIDDEN",
        34: "FORBIDDEN",
        35: "FORBIDDEN",
        36: "FORBIDDEN",
        37: "FORBIDDEN",
        38: "AVAILABLE_WITH_REVIEW",
        43: "RESERVED",
        44: "RESERVED",
        45: "SENSITIVE",
        46: "SENSITIVE",
        48: "RESERVED",
    }
    for gpio, expected in expected_pin_classes.items():
        actual = pins.get(gpio, {}).get("classification")
        if actual != expected:
            error(f"GPIO{gpio} must be classified as {expected}, found {actual!r}")
    for gpio in range(22, 38):
        if pins.get(gpio, {}).get("verification_status") != "OFFICIAL_DATASHEET":
            error(f"GPIO{gpio} existence/memory restriction must use OFFICIAL_DATASHEET status")

    required_signals = {
        "SHT30_3V3",
        "GY63_3V3",
        "COMMON_GND",
        "I2C_SDA",
        "I2C_SCL",
        "USB_D_MINUS",
        "USB_D_PLUS",
        "MS5611_CSB_LOW",
        "MS5611_PS_HIGH",
        "MS5611_SDO_NC",
        "SYSTEM_5V",
        "USB_VBUS_BLOCKED",
        "USB_SIGNAL_GND",
    }
    seen_signals = {row.get("signal_name", "") for row in connections}
    for signal in sorted(required_signals - seen_signals):
        error(f"Required signal missing from project_wiring.csv: {signal}")

    dangerous_output = re.compile(r"\b(?:MOTOR|ESC|SERVO|PWM|MCPWM|DSHOT|RMT)\b", re.IGNORECASE)
    allowed_reserved_usage = {
        8: {"I2C_SDA"},
        9: {"I2C_SCL"},
        19: {"USB_D_MINUS"},
        20: {"USB_D_PLUS"},
    }
    for line_number, row in enumerate(connections, 2):
        status = row.get("verification_status")
        if status not in ALLOWED_STATUSES:
            error(f"{relative(connections_path)}:{line_number}: invalid verification_status {status!r}")
        if row.get("requirement") not in {"required", "optional"}:
            error(f"{relative(connections_path)}:{line_number}: requirement must be required or optional")
        joined = " ".join(str(value) for value in row.values())
        if dangerous_output.search(joined):
            error(f"Physical actuator/output wiring is forbidden in this phase: connection {row.get('connection_id')}")
        references = gpio_references(row)
        for gpio in references:
            classification = pins.get(gpio, {}).get("classification")
            if classification is None:
                error(f"Connection {row.get('connection_id')} references GPIO{gpio}, which is absent from gpio_usage_rules.csv")
            if classification in {"FORBIDDEN", "NONEXISTENT"}:
                error(f"Connection {row.get('connection_id')} uses unavailable GPIO{gpio}")
            if gpio in {0, 3, 38, 43, 44, 45, 46, 48}:
                error(f"Connection {row.get('connection_id')} uses reserved/sensitive GPIO{gpio}")
            if gpio in allowed_reserved_usage and row.get("signal_name") not in allowed_reserved_usage[gpio]:
                error(
                    f"Connection {row.get('connection_id')} uses GPIO{gpio} for {row.get('signal_name')}, "
                    f"allowed only for {sorted(allowed_reserved_usage[gpio])}"
                )

    by_signal: dict[str, list[dict[str, str]]] = {}
    for row in connections:
        by_signal.setdefault(row.get("signal_name", ""), []).append(row)

    def require_signal_row(signal: str, predicate: Any, description: str) -> None:
        if not any(predicate(row) for row in by_signal.get(signal, [])):
            error(f"{signal} must {description}")

    require_signal_row("I2C_SDA", lambda row: row.get("source_gpio_or_net") == "GPIO8", "originate at GPIO8")
    require_signal_row("I2C_SCL", lambda row: row.get("source_gpio_or_net") == "GPIO9", "originate at GPIO9")
    require_signal_row("USB_D_MINUS", lambda row: row.get("destination_pin_label") == "GPIO19", "terminate at GPIO19")
    require_signal_row("USB_D_PLUS", lambda row: row.get("destination_pin_label") == "GPIO20", "terminate at GPIO20")
    require_signal_row(
        "SHT30_3V3",
        lambda row: row.get("destination_component") == "sht30_ambient"
        and row.get("destination_pin_label") == "VCC"
        and row.get("voltage_domain") == "3.3V",
        "power SHT VCC from the 3.3 V domain",
    )
    require_signal_row(
        "GY63_3V3",
        lambda row: row.get("destination_component") == "ms5611_baro"
        and row.get("destination_pin_label") == "VCC"
        and row.get("voltage_domain") == "3.3V",
        "power GY-63 VCC from the 3.3 V domain",
    )
    require_signal_row(
        "MS5611_PS_HIGH",
        lambda row: row.get("destination_pin_label") == "PS"
        and row.get("source_gpio_or_net") == "3V3"
        and row.get("voltage_domain") == "3.3V",
        "define PS high from 3V3 for I2C mode",
    )
    require_signal_row(
        "MS5611_CSB_LOW",
        lambda row: row.get("destination_pin_label") == "CSB"
        and row.get("source_gpio_or_net") == "GND"
        and row.get("voltage_domain") == "0V",
        "tie CSB low without floating",
    )
    require_signal_row(
        "MS5611_SDO_NC",
        lambda row: row.get("destination_pin_label") == "SDO"
        and row.get("source_gpio_or_net") == "NC"
        and row.get("direction") == "not_connected",
        "show SDO explicitly not connected",
    )
    require_signal_row(
        "SYSTEM_5V",
        lambda row: row.get("source_component") == "battery_5v"
        and row.get("voltage_domain") == "5V"
        and "TO_BE_VERIFIED" in row.get("destination_pin_label", "")
        and "UNVERIFIED" in row.get("notes", ""),
        "originate at the dedicated battery and keep the exact 5V/VIN point visibly unverified",
    )
    require_signal_row(
        "USB_VBUS_BLOCKED",
        lambda row: row.get("source_component") == "usb_host"
        and row.get("source_pin_label") == "USB VBUS"
        and row.get("destination_component") == "required_vbus_isolation_boundary"
        and row.get("direction") == "blocked_power_path"
        and "UNVERIFIED" in row.get("notes", ""),
        "terminate at an explicitly required but unverified isolation boundary",
    )
    require_signal_row(
        "USB_SIGNAL_GND",
        lambda row: row.get("source_component") == "usb_host"
        and row.get("destination_pin_label") == "GND",
        "provide the USB signal-ground reference",
    )

    for row in connections:
        joined = " ".join(str(value) for value in row.values())
        if row.get("source_component") == "battery_5v" and row.get("destination_component") in {
            "sht30_ambient",
            "ms5611_baro",
        }:
            error("The 5 V battery must never connect directly to a sensor VCC")
        if row.get("source_component") == "usb_host" and row.get("source_pin_label") == "USB VBUS":
            if row.get("destination_component") in {"main_esp32", "sht30_ambient", "ms5611_baro", "battery_5v"}:
                error("USB-host VBUS must never connect to the board, sensors, or battery rail for power")
            if row.get("direction") in {"power", "bidirectional"}:
                error("USB-host VBUS row must represent a blocked power path")
        if re.search(r"\bVerified\s+(?:5V|VIN|input|connector|path|isolation)\b", joined, re.IGNORECASE):
            error(f"Connection {row.get('connection_id')} overstates unverified physical evidence")

    return connections, pins


def validate_diagrams() -> None:
    required_diagrams = {
        "02_i2c_bus.mmd": ("GPIO8", "GPIO9", "UNVERIFIED"),
        "03_usb_ports.mmd": ("GPIO19", "GPIO20", "UNVERIFIED"),
        "05_gpio_usage.mmd": ("GPIO22", "GPIO25", "GPIO26", "GPIO34", "GPIO35", "GPIO37", "NONEXISTENT", "FORBIDDEN", "UNVERIFIED"),
        "04_power_and_usb_vbus.mmd": ("VBUS", "UNVERIFIED"),
        "01_system_wiring.mmd": ("GPIO8", "GPIO9", "GPIO19", "GPIO20", "UNVERIFIED", "fail-safe supervised", "default disabled"),
    }
    risky_output = re.compile(r"\b(?:MOTOR|ESC|SERVO|PWM|MCPWM|DSHOT|RMT)\b", re.IGNORECASE)
    overclaim = re.compile(r"\bverified\s+(?:native|USB|connector|5V|VIN|input|path|isolation)", re.IGNORECASE)
    for name, terms in required_diagrams.items():
        path = DIAGRAM_SOURCES / name
        require_file(path)
        if not path.is_file():
            continue
        content = path.read_text(encoding="utf-8-sig")
        for term in terms:
            if term not in content:
                error(f"{relative(path)} must visibly include {term!r}")
        if overclaim.search(content):
            error(f"{relative(path)} presents an unverified physical path as verified")
        for line_number, line in enumerate(content.splitlines(), 1):
            if risky_output.search(line) and not re.search(
                r"\b(?:disabled|forbidden|prohibited|not allowed|no physical)\b", line, re.IGNORECASE
            ):
                error(f"{relative(path)}:{line_number}: forbidden actuator/output feature appears enabled or ambiguous")
        if name == "05_gpio_usage.mmd" and "All other pins" in content:
            error("Pin-safety diagram must not imply all omitted pins are available")

    docs_root = ROOT / "doc"
    source_paths: list[Path] = []
    if docs_root.is_dir():
        markdown_image = re.compile(r"!?\[[^\]]*\]\(([^)]+\.(?:svg|png))\)", re.IGNORECASE)
        for markdown in docs_root.rglob("*.md"):
            content = markdown.read_text(encoding="utf-8-sig")
            for raw_target in markdown_image.findall(content):
                target_text = raw_target.split(maxsplit=1)[0].strip("<>\"")
                if re.match(r"^[a-z]+://", target_text, re.IGNORECASE):
                    continue
                target = ROOT / target_text if target_text.startswith("doc/") else markdown.parent / target_text
                if not target.resolve().is_file():
                    error(f"{relative(markdown)} references missing figure {target_text}")

        if (docs_root / "diagrams").is_dir():
            source_paths.extend((docs_root / "diagrams").rglob("*.mmd"))
    source_paths.extend(DIAGRAM_SOURCES.glob("*.mmd"))
    source_stems = {path.stem for path in source_paths}
    if (docs_root / "diagrams").is_dir():
        for suffix in ("*.svg", "*.png"):
            for rendered in (docs_root / "diagrams").rglob(suffix):
                if rendered.stem not in source_stems:
                    error(f"Rendered figure has no same-named editable Mermaid source: {relative(rendered)}")


def main() -> int:
    required_manifests = {
        "hardware": MANIFESTS / "project_hardware_profile.yaml",
        "connections": MANIFESTS / "project_wiring.csv",
        "pins": MANIFESTS / "gpio_usage_rules.csv",
        "photos": MANIFESTS / "hardware_photos.yaml",
        "measurements": MANIFESTS / "hardware_measurements.yaml",
    }
    for path in required_manifests.values():
        require_file(path)

    parsed_yaml: dict[str, Any] = {}
    for name in ("hardware", "photos", "measurements"):
        path = required_manifests[name]
        if not path.is_file():
            continue
        try:
            parsed_yaml[name] = load_yaml_subset(path)
        except (OSError, UnicodeError, YamlSubsetError) as exc:
            error(f"Unable to parse {relative(path)}: {exc}")

    if "hardware" in parsed_yaml:
        validate_hardware_manifest(parsed_yaml["hardware"])
    if "photos" in parsed_yaml:
        validate_photos(parsed_yaml["photos"])
    if "measurements" in parsed_yaml:
        validate_measurements(parsed_yaml["measurements"])
    if "hardware" in parsed_yaml and "measurements" in parsed_yaml:
        validate_cross_manifest_evidence(parsed_yaml["hardware"], parsed_yaml["measurements"])

    validate_csv_manifests()
    validate_official_hashes()
    validate_diagrams()

    for message in WARNINGS:
        print(f"WARNING: {message}")
    for message in ERRORS:
        print(f"ERROR: {message}", file=sys.stderr)

    if ERRORS:
        print(
            f"Validation failed with {len(ERRORS)} error(s) and {len(WARNINGS)} warning(s).",
            file=sys.stderr,
        )
        return 1

    print(
        f"Validation passed with {len(WARNINGS)} warning(s). "
        "Logical diagrams are internally consistent; physical verification remains required."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
