#!/usr/bin/env python3
"""Cross-check firmware defaults/runtime contract against hardware manifests.

This validator is intentionally fail-closed for production safety configuration
and evidence-backed Kconfig claims.
It is safe to run without an ESP-IDF sdkconfig (host/CI default-contract check),
and gains build-authorization checks when --sdkconfig is provided.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from validate_graphics_inputs import load_yaml_subset  # noqa: E402

PROFILE = ROOT / "hardware_reference/04_project_hardware_configuration/project_hardware_profile.yaml"
PHOTOS = ROOT / "hardware_reference/04_project_hardware_configuration/hardware_photos.yaml"
MEASUREMENTS = ROOT / "hardware_reference/04_project_hardware_configuration/hardware_measurements.yaml"

ERRORS: list[str] = []


def fail(message: str) -> None:
    ERRORS.append(message)


def text(rel: str) -> str:
    path = ROOT / rel
    if not path.is_file():
        fail(f"required file missing: {rel}")
        return ""
    return path.read_text(encoding="utf-8", errors="replace")


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


def int_literal(source: str, pattern: str, label: str) -> int | None:
    match = re.search(pattern, source, flags=re.MULTILINE | re.DOTALL)
    if not match:
        fail(f"cannot locate {label}")
        return None
    return int(match.group(1).replace("'", ""), 0)


def strip_cmake_comments(source: str) -> str:
    """Remove CMake comments while preserving command offsets."""
    source = re.sub(
        r"#\[(=*)\[.*?\]\1\]",
        lambda match: " " * len(match.group(0)),
        source,
        flags=re.DOTALL,
    )
    cleaned: list[str] = []
    in_quote = False
    in_line_comment = False
    escaped = False
    for character in source:
        if character == "\n":
            cleaned.append(character)
            in_line_comment = False
            escaped = False
            continue
        if in_line_comment:
            cleaned.append(" ")
            continue
        if character == '"' and not escaped:
            in_quote = not in_quote
        if character == "#" and not in_quote:
            cleaned.append(" ")
            in_line_comment = True
            escaped = False
            continue
        cleaned.append(character)
        escaped = character == "\\" and not escaped
        if character != "\\":
            escaped = False
    return "".join(cleaned)


def cmake_commands(source: str) -> list[tuple[str, str, int]]:
    """Extract CMake command name, argument text, and source offset."""
    commands: list[tuple[str, str, int]] = []
    source = strip_cmake_comments(source)
    cursor = 0
    command_start = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
    while cursor < len(source):
        match = command_start.search(source, cursor)
        if match is None:
            break
        body_start = match.end()
        while body_start < len(source) and source[body_start].isspace():
            body_start += 1
        if body_start >= len(source) or source[body_start] != "(":
            cursor = match.end()
            continue
        depth = 1
        index = body_start + 1
        in_quote = False
        escaped = False
        while index < len(source) and depth:
            character = source[index]
            if character == '"' and not escaped:
                in_quote = not in_quote
            elif not in_quote:
                if character == "(":
                    depth += 1
                elif character == ")":
                    depth -= 1
            escaped = character == "\\" and not escaped
            if character != "\\":
                escaped = False
            index += 1
        if depth:
            commands.append((match.group(0).lower(), source[body_start + 1:], match.start()))
            break
        commands.append((match.group(0).lower(), source[body_start + 1:index - 1], match.start()))
        cursor = index
    return commands


def cmake_arguments(body: str) -> list[str]:
    """Tokenize the simple argument forms allowed by the root build contract."""
    return [
        token[1:-1] if len(token) >= 2 and token[0] == token[-1] == '"' else token
        for token in re.findall(r'"(?:\\.|[^"\\])*"|[^\s]+', body)
    ]


def production_component_seed_errors(source: str) -> list[str]:
    """Validate the fail-closed ESP-IDF production component seed."""
    errors: list[str] = []
    commands = cmake_commands(source)
    component_sets = [
        (arguments, offset)
        for name, body, offset in commands
        if name == "set" and (arguments := cmake_arguments(body))
        and arguments[0] == "COMPONENTS"
    ]
    project_includes = []
    for name, body, offset in commands:
        if name != "include":
            continue
        arguments = cmake_arguments(body)
        if arguments and arguments[0].replace("\\", "/") == "$ENV{IDF_PATH}/tools/cmake/project.cmake":
            project_includes.append(offset)

    if len(component_sets) != 1 or component_sets[0][0] != ["COMPONENTS", "main"]:
        errors.append(
            "root CMakeLists.txt must contain exactly one normal set(COMPONENTS main) production seed"
        )
    if len(project_includes) != 1:
        errors.append(
            "root CMakeLists.txt must include $ENV{IDF_PATH}/tools/cmake/project.cmake exactly once"
        )
    if component_sets and project_includes and component_sets[0][1] >= project_includes[0]:
        errors.append("set(COMPONENTS main) must appear before the ESP-IDF project.cmake include")

    forbidden_controls = {
        "EXCLUDE_COMPONENTS",
        "TEST_COMPONENTS",
        "TEST_EXCLUDE_COMPONENTS",
        "TESTS_ALL",
        "BUILD_TESTS",
    }
    active_source = strip_cmake_comments(source)
    for variable in sorted(forbidden_controls):
        if re.search(rf"(?<![A-Za-z0-9_]){variable}(?![A-Za-z0-9_])", active_source):
            errors.append(f"root CMakeLists.txt must not use test/build graph control {variable}")
    if re.search(r"(?<![A-Za-z0-9_])MINIMAL_BUILD(?![A-Za-z0-9_])", active_source):
        errors.append("MINIMAL_BUILD is unsupported by the pinned ESP-IDF 5.4.4 build system")

    for name, body, _offset in commands:
        arguments = cmake_arguments(body)
        if not arguments:
            continue
        mutates_components = (
            (name == "unset" and arguments[0] == "COMPONENTS")
            or (name == "list" and len(arguments) > 1 and arguments[1] == "COMPONENTS")
            or (name == "string" and len(arguments) > 1 and arguments[1] == "COMPONENTS")
        )
        if mutates_components:
            errors.append("root CMakeLists.txt must not mutate COMPONENTS after its fixed main seed")
    return errors


def parse_sdkconfig(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    not_set = re.compile(r"^#\s*(CONFIG_[A-Za-z0-9_]+)\s+is not set$")
    if not path.is_file():
        fail(f"sdkconfig requested but missing: {path}")
        return values
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw.strip()
        match = not_set.match(line)
        if match:
            values[match.group(1)] = "n"
            continue
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key.strip()] = value.strip().strip('"')
    return values


def cfg_true(config: dict[str, str], key: str) -> bool:
    return config.get(key) == "y"


def cfg_string(config: dict[str, str], key: str) -> str:
    return config.get(key, "")


def cfg_int(config: dict[str, str], key: str) -> int | None:
    raw = config.get(key)
    if raw is None:
        return None
    try:
        return int(raw, 0)
    except ValueError:
        fail(f"invalid integer value for {key}: {raw!r}")
        return None


def load_evidence() -> tuple[dict[str, dict[str, Any]], dict[str, Any]]:
    photos_doc = load_yaml_subset(PHOTOS)
    measurements = load_yaml_subset(MEASUREMENTS)
    photos: dict[str, dict[str, Any]] = {}
    for record in photos_doc.get("photos", []):
        if isinstance(record, dict) and isinstance(record.get("id"), str):
            photos[record["id"]] = record
    return photos, measurements


def actual_photo(record: dict[str, Any] | None) -> bool:
    return bool(record and record.get("present") is True and
                record.get("status") in {"USER_PHOTO", "VERIFIED_MEASUREMENT"})


def photo_purpose(record: dict[str, Any] | None, terms: tuple[str, ...]) -> bool:
    if not actual_photo(record):
        return False
    purpose = str(record.get("required_for", "")).lower()
    return any(term in purpose for term in terms)


def measurement_record(measurements: dict[str, Any]) -> tuple[str, bool]:
    meta = measurements.get("measurement_metadata", {})
    record_id = meta.get("evidence_record_id") if isinstance(meta, dict) else None
    verified = bool(isinstance(meta, dict) and meta.get("status") == "VERIFIED_MEASUREMENT" and
                    isinstance(record_id, str) and record_id)
    return (record_id if isinstance(record_id, str) else "", verified)


def validate_evidence_config(config: dict[str, str]) -> None:
    photos, measurements = load_evidence()
    measure_id, measurements_verified = measurement_record(measurements)

    def id_photo(key: str) -> dict[str, Any] | None:
        return photos.get(cfg_string(config, key))

    if cfg_true(config, "CONFIG_SHAHBAZ_BOARD_REVISION_YD_ESP32_S3_V1_4"):
        rec = id_photo("CONFIG_SHAHBAZ_BOARD_REVISION_EVIDENCE_RECORD_ID")
        require(photo_purpose(rec, ("board_identity", "board_revision")),
                "verified board revision requires a present actual-board photo evidence ID")

    if cfg_true(config, "CONFIG_SHAHBAZ_N16R8_MODULE_IDENTITY_VERIFIED"):
        rec = id_photo("CONFIG_SHAHBAZ_MODULE_IDENTITY_EVIDENCE_RECORD_ID")
        require(photo_purpose(rec, ("board_identity", "module")),
                "N16R8 module identity requires a present actual-board/module photo evidence ID")

    configured_sda = cfg_int(config, "CONFIG_SHAHBAZ_I2C_SDA_GPIO")
    configured_scl = cfg_int(config, "CONFIG_SHAHBAZ_I2C_SCL_GPIO")
    alternate_i2c_selected = (
        configured_sda is not None and configured_scl is not None and
        (configured_sda, configured_scl) != (8, 9)
    )
    alternate_i2c_reviewed = cfg_true(config, "CONFIG_SHAHBAZ_ALT_I2C_PINS_PHYSICALLY_REVIEWED")
    if alternate_i2c_selected:
        require(alternate_i2c_reviewed,
                "non-default I2C GPIOs require CONFIG_SHAHBAZ_ALT_I2C_PINS_PHYSICALLY_REVIEWED=y")
    if alternate_i2c_reviewed:
        rec = id_photo("CONFIG_SHAHBAZ_ALT_I2C_EVIDENCE_RECORD_ID")
        require(photo_purpose(rec, ("gpio", "routing", "board_revision", "board_identity")),
                "alternate I2C pins require present exact-board GPIO/routing evidence")

    actuators_enabled = cfg_true(config, "CONFIG_SHAHBAZ_ACTUATORS_ENABLE")
    actuator_reviewed = cfg_true(config, "CONFIG_SHAHBAZ_ACTUATOR_PINS_PHYSICALLY_REVIEWED")
    if actuators_enabled:
        require(actuator_reviewed,
                "enabled physical actuators require CONFIG_SHAHBAZ_ACTUATOR_PINS_PHYSICALLY_REVIEWED=y")
    if actuator_reviewed:
        rec = id_photo("CONFIG_SHAHBAZ_ACTUATOR_EVIDENCE_RECORD_ID")
        require(photo_purpose(rec, ("actuator", "gpio", "pwm")),
                "actuator GPIO authorization requires present exact-board actuator/GPIO evidence")

    if cfg_true(config, "CONFIG_SHAHBAZ_SENSOR_HARDWARE_VERIFIED"):
        supplied = cfg_string(config, "CONFIG_SHAHBAZ_SENSOR_EVIDENCE_RECORD_ID")
        require(measurements_verified and supplied == measure_id,
                "sensor hardware verification requires the VERIFIED_MEASUREMENT record ID from hardware_measurements.yaml")

    usb_flags = (
        "CONFIG_SHAHBAZ_NATIVE_USB_ROUTE_VERIFIED",
        "CONFIG_SHAHBAZ_BOARD_POWER_PATH_VERIFIED",
        "CONFIG_SHAHBAZ_USB_VBUS_ISOLATION_VERIFIED",
        "CONFIG_SHAHBAZ_USB_BIDIRECTIONAL_REVERSE_CURRENT_VERIFIED",
        "CONFIG_SHAHBAZ_USB_ISOLATED_ENUMERATION_VERIFIED",
    )
    if any(cfg_true(config, key) for key in usb_flags):
        supplied = cfg_string(config, "CONFIG_SHAHBAZ_USB_EVIDENCE_RECORD_ID")
        require(measurements_verified and supplied == measure_id,
                "USB/power verification flags require the VERIFIED_MEASUREMENT record ID from hardware_measurements.yaml")


def validate_build_config(
    config: dict[str, str], actuator_backend: str = "null"
) -> None:
    validate_evidence_config(config)
    require(actuator_backend in {"null", "espidf"},
            f"unsupported actuator backend: {actuator_backend!r}")
    require("CONFIG_BT_ENABLED" not in config,
            "trimmed production sdkconfig must omit unavailable CONFIG_BT_ENABLED")
    actuators_enabled = config.get("CONFIG_SHAHBAZ_ACTUATORS_ENABLE", "n") == "y"
    if actuator_backend == "null":
        require(not actuators_enabled,
                "null actuator profile requires CONFIG_SHAHBAZ_ACTUATORS_ENABLE=n")
    elif actuator_backend == "espidf":
        require(actuators_enabled,
                "espidf actuator profile requires CONFIG_SHAHBAZ_ACTUATORS_ENABLE=y")
    require(cfg_int(config, "CONFIG_SHAHBAZ_HEARTBEAT_TIMEOUT_MS") == 1000,
            "configured production build must use the reviewed 1000 ms heartbeat timeout")
    require(cfg_int(config, "CONFIG_SHAHBAZ_CONTROL_COMMAND_TIMEOUT_MS") == 250,
            "configured production build must use the reviewed 250 ms control-command timeout")
    require(cfg_true(config, "CONFIG_ESP_CONSOLE_UART_DEFAULT"),
            "production diagnostics must keep the primary console on UART0")
    require(cfg_true(config, "CONFIG_ESP_CONSOLE_SECONDARY_NONE") and
            not cfg_true(config, "CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG") and
            not cfg_true(config, "CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED"),
            "native TinyUSB production build must disable the secondary USB Serial/JTAG console")



def validate_evidence_guard_self_test() -> None:
    """Regression-check that actuator authorization fails closed."""
    before = len(ERRORS)
    validate_evidence_config({"CONFIG_SHAHBAZ_ACTUATORS_ENABLE": "y"})
    missing_review_errors = ERRORS[before:]
    del ERRORS[before:]
    require(
        any("enabled physical actuators require" in message for message in missing_review_errors),
        "evidence authorization regression self-test failed: actuator enable without review was accepted",
    )

    before = len(ERRORS)
    validate_evidence_config({
        "CONFIG_SHAHBAZ_ACTUATORS_ENABLE": "y",
        "CONFIG_SHAHBAZ_ACTUATOR_PINS_PHYSICALLY_REVIEWED": "y",
        "CONFIG_SHAHBAZ_ACTUATOR_EVIDENCE_RECORD_ID": "__invented_evidence_id__",
    })
    invented_id_errors = ERRORS[before:]
    del ERRORS[before:]
    require(
        any("actuator GPIO authorization" in message for message in invented_id_errors),
        "evidence authorization regression self-test failed: invented actuator evidence was accepted",
    )


def validate_production_component_seed_self_test() -> None:
    """Regression-check that only the production root can seed ESP-IDF."""
    include = "include($ENV{IDF_PATH}/tools/cmake/project.cmake)"
    valid = (
        "cmake_minimum_required(VERSION 3.16)\n"
        "# EXCLUDE_COMPONENTS and TEST_COMPONENTS in comments are inert.\n"
        "set(COMPONENTS main)\n"
        f"{include}\n"
        "project(shahbaz_sensor_usb_node)\n"
    )
    require(
        not production_component_seed_errors(valid),
        "production component seed regression self-test failed: the exact main seed was rejected",
    )

    invalid_cases = {
        "additional test framework seed": valid.replace(
            "set(COMPONENTS main)", "set(COMPONENTS main unity)"
        ),
        "test-framework exclusion workaround": valid.replace(
            include, f"set(EXCLUDE_COMPONENTS unity cmock idf_test)\n{include}"
        ),
        "test-component selection": valid.replace(
            include, f"set(TEST_COMPONENTS sensor_sht30)\n{include}"
        ),
        "seed after project include": valid.replace(
            f"set(COMPONENTS main)\n{include}", f"{include}\nset(COMPONENTS main)"
        ),
        "post-seed component mutation": valid.replace(
            include, f"list(APPEND COMPONENTS unity)\n{include}"
        ),
        "unsupported minimal-build switch": valid.replace(
            "set(COMPONENTS main)", "set(MINIMAL_BUILD ON)\nset(COMPONENTS main)"
        ),
    }
    for label, source in invalid_cases.items():
        require(
            bool(production_component_seed_errors(source)),
            f"production component seed regression self-test failed: accepted {label}",
        )


def validate_trimmed_config_self_test() -> None:
    """Regression-check trimmed config and fail-closed actuator invariants."""
    before = len(ERRORS)
    validate_build_config({
        "CONFIG_SHAHBAZ_ACTUATORS_ENABLE": "n",
        "CONFIG_SHAHBAZ_HEARTBEAT_TIMEOUT_MS": "1000",
        "CONFIG_SHAHBAZ_CONTROL_COMMAND_TIMEOUT_MS": "250",
        "CONFIG_ESP_CONSOLE_UART_DEFAULT": "y",
        "CONFIG_ESP_CONSOLE_SECONDARY_NONE": "y",
        "CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG": "n",
        "CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED": "n",
    })
    clean_errors = ERRORS[before:]
    del ERRORS[before:]
    require(
        not clean_errors,
        f"trimmed config regression self-test rejected clean config: {clean_errors}",
    )

    invalid_cases = {
        "stale unavailable Bluetooth symbol": {
            "CONFIG_BT_ENABLED": "n",
            "CONFIG_SHAHBAZ_ACTUATORS_ENABLE": "n",
            "CONFIG_SHAHBAZ_HEARTBEAT_TIMEOUT_MS": "1000",
            "CONFIG_SHAHBAZ_CONTROL_COMMAND_TIMEOUT_MS": "250",
            "CONFIG_ESP_CONSOLE_UART_DEFAULT": "y",
            "CONFIG_ESP_CONSOLE_SECONDARY_NONE": "y",
            "CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG": "n",
            "CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED": "n",
        },
        "enabled actuators in null profile": {
            "CONFIG_SHAHBAZ_ACTUATORS_ENABLE": "y",
            "CONFIG_SHAHBAZ_HEARTBEAT_TIMEOUT_MS": "1000",
            "CONFIG_SHAHBAZ_CONTROL_COMMAND_TIMEOUT_MS": "250",
            "CONFIG_ESP_CONSOLE_UART_DEFAULT": "y",
            "CONFIG_ESP_CONSOLE_SECONDARY_NONE": "y",
            "CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG": "n",
            "CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED": "n",
        },
        "secondary USB Serial JTAG console with TinyUSB": {
            "CONFIG_SHAHBAZ_ACTUATORS_ENABLE": "n",
            "CONFIG_SHAHBAZ_HEARTBEAT_TIMEOUT_MS": "1000",
            "CONFIG_SHAHBAZ_CONTROL_COMMAND_TIMEOUT_MS": "250",
            "CONFIG_ESP_CONSOLE_UART_DEFAULT": "y",
            "CONFIG_ESP_CONSOLE_SECONDARY_NONE": "n",
            "CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG": "y",
            "CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED": "y",
        },
    }
    for label, config in invalid_cases.items():
        before = len(ERRORS)
        validate_build_config(config)
        case_errors = ERRORS[before:]
        del ERRORS[before:]
        require(
            bool(case_errors),
            f"trimmed config regression self-test accepted {label}",
        )

    before = len(ERRORS)
    validate_build_config({
        "CONFIG_SHAHBAZ_ACTUATORS_ENABLE": "n",
        "CONFIG_SHAHBAZ_HEARTBEAT_TIMEOUT_MS": "1000",
        "CONFIG_SHAHBAZ_CONTROL_COMMAND_TIMEOUT_MS": "250",
    }, actuator_backend="espidf")
    physical_mismatch_errors = ERRORS[before:]
    del ERRORS[before:]
    require(
        any("espidf actuator profile requires" in message
            for message in physical_mismatch_errors),
        "trimmed config regression self-test accepted an espidf/Kconfig mismatch",
    )


def validate_contract() -> None:
    profile = load_yaml_subset(PROFILE)
    board = profile["board"]
    i2c = board["i2c"]
    native_usb = board["usb_ports"]["native_usb_otg"]
    sensors = {entry["id"]: entry for entry in profile["sensors"]}

    root_cmake = text("CMakeLists.txt")
    for message in production_component_seed_errors(root_cmake):
        fail(message)
    board_hpp = text("components/board_support/include/shahbaz/board/board_profile.hpp")
    board_kconfig = text("components/board_support/Kconfig")
    safety_kconfig = text("components/safety_supervisor/Kconfig")
    main_cmake = text("main/CMakeLists.txt")
    main_kconfig = text("main/Kconfig.projbuild")
    actuator_kconfig = text("components/actuator_espidf/Kconfig")
    app_main = text("main/app_main.cpp")
    sht = text("components/sensor_sht30/include/sensor_sht30/sht3x_domain.hpp")
    ms = text("components/sensor_ms5611/include/sensor_ms5611/ms5611_domain.hpp")
    wire = text("components/telemetry_protocol/include/shahbaz/protocol/wire_protocol.hpp")
    kotlin = text("android_reference/src/main/kotlin/com/shahbaz/protocol/ProvisionalProtocol.kt")
    hil = text("tools/windows_hil_test.py")
    engine = text("components/device_link/src/protocol_engine.cpp")
    usb = text("components/usb_transport_espidf/src/espidf_usb_cdc_transport.cpp")
    i2c_cpp = text("components/platform_espidf/src/espidf_i2c_bus.cpp")
    defaults = text("sdkconfig.defaults")
    default_config = parse_sdkconfig(ROOT / "sdkconfig.defaults")

    flash = int_literal(board_hpp, r"kExpectedFlashBytes\s*=\s*([0-9']+)U\s*\*\s*1024U\s*\*\s*1024U", "expected flash MiB")
    psram = int_literal(board_hpp, r"kExpectedPsramBytes\s*=\s*([0-9']+)U\s*\*\s*1024U\s*\*\s*1024U", "expected PSRAM MiB")
    require(flash == int(board["flash_mb"]), "firmware expected flash size disagrees with hardware profile")
    require(psram == int(board["psram_mb"]), "firmware expected PSRAM size disagrees with hardware profile")

    def kconfig_default(name: str) -> int | None:
        return int_literal(board_kconfig, rf"config\s+{re.escape(name)}\b.*?\bdefault\s+([0-9]+)", f"Kconfig default {name}")

    require(kconfig_default("SHAHBAZ_I2C_SDA_GPIO") == int(i2c["sda_gpio"]), "I2C SDA Kconfig default disagrees with profile")
    require(kconfig_default("SHAHBAZ_I2C_SCL_GPIO") == int(i2c["scl_gpio"]), "I2C SCL Kconfig default disagrees with profile")
    require("400'000U" in app_main or "400000U" in app_main,
            "app_main no longer instantiates the project 400 kHz I2C target")
    require(int(i2c["target_frequency_hz"]) == 400000, "hardware profile I2C target unexpectedly changed")

    require(f"kUsbDataMinusGpio = {int(native_usb['d_minus_gpio'])}" in board_hpp,
            "USB D- GPIO disagrees with hardware profile")
    require(f"kUsbDataPlusGpio = {int(native_usb['d_plus_gpio'])}" in board_hpp,
            "USB D+ GPIO disagrees with hardware profile")

    require("kDefaultAddress = 0x44" in sht and "kAlternateAddress = 0x45" in sht,
            "SHT3x firmware address set changed")
    require(sensors["sht30_ambient"]["configured_address_7bit"] == "0x44",
            "SHT30 configured address disagrees with firmware default")
    require("kAddressCsbLow = 0x77" in ms and "kAddressCsbHigh = 0x76" in ms,
            "MS5611 firmware CSB/address map changed")
    require(sensors["ms5611_baro"]["configured_address_7bit"] == "0x77",
            "MS5611 configured address disagrees with firmware default")

    cpp_version = int_literal(wire, r"kProtocolVersion\s*=\s*([0-9]+)U", "C++ protocol version")
    kotlin_version = int_literal(kotlin, r"VERSION:\s*Int\s*=\s*([0-9]+)", "Kotlin protocol version")
    hil_version = int_literal(hil, r"PROTOCOL_VERSION\s*=\s*([0-9]+)", "Python HIL protocol version")
    require(cpp_version == kotlin_version == hil_version == 2,
            f"protocol implementation versions are inconsistent: C++={cpp_version}, Kotlin={kotlin_version}, HIL={hil_version}")

    require("validateAndStripSessionToken" in engine and "SessionMismatch" in engine and
            "senderFreshness" in engine and "maximum_sender_age_us" in engine,
            "ProtocolEngine is missing v2 session/freshness enforcement")
    require(
        "resetSession" in usb and
        "revokeSession();" in usb and
        "xQueueReceive(rx_queue_" in usb and
        "chunk.epoch = before.epoch" in usb and
        "active_rx_.epoch != connection.epoch" in usb and
        "clearTinyUsbBuffers();" in usb,
        "USB transport is missing epoch-isolated stale-RX session reset",
    )
    require("configurationAuthorized()" in i2c_cpp and "is_valid_i2c_pair" in i2c_cpp,
            "I2C adapter does not enforce board GPIO policy internally")
    recovery_source = i2c_cpp.split("auto EspIdfI2cBus::recover", maxsplit=1)
    timeout_restore = re.search(
        r"auto\s+EspIdfI2cBus::restoreAfterTimedOutRecovery\(\)\s+noexcept\s*"
        r"->\s*interfaces::I2cStatus\s*\{\s*"
        r"(?://[^\n]*\n\s*)*"
        r"const\s+auto\s+restore_status\s*=\s*initialize\(\);\s*"
        r"return\s+restore_status\s*==\s*interfaces::I2cStatus::Ok\s*"
        r"\?\s*interfaces::I2cStatus::Timeout\s*"
        r":\s*interfaces::I2cStatus::RecoveryFailed;\s*\}",
        i2c_cpp,
        flags=re.MULTILINE,
    )
    require(
        len(recovery_source) == 2 and
        recovery_source[1].count("return restoreAfterTimedOutRecovery();") == 2 and
        "return interfaces::I2cStatus::Timeout;" not in recovery_source[1] and
        timeout_restore is not None,
        "timed-out I2C recovery must restore the ESP-IDF bus before returning Timeout",
    )
    require(
        "CONFIG_ESP_TASK_WDT_EN=y" in defaults and
        "CONFIG_ESP_TASK_WDT_PANIC=y" in defaults and
        "CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0=y" in defaults and
        "CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1=y" in defaults,
        "task watchdog must remain panic/reset capable with both idle-starvation checks",
    )
    require(
        "CONFIG_FREERTOS_HZ=1000" in defaults,
        "the 1 ms service loop and 5 ms I2C deadlines require a 1000 Hz FreeRTOS tick",
    )
    require("esp_task_wdt_add(nullptr)" in app_main and "TaskHealthMonitor" in app_main,
            "runtime watchdog/health monitoring is not wired into app_main")
    require(
        re.search(r"constexpr\s+TickType_t\s+kLoopDelayTicks\s*=\s*1U", app_main) is not None and
        "vTaskDelay(kLoopDelayTicks)" in app_main and
        "pdMS_TO_TICKS(kLoopDelayMs)" not in app_main,
        "app_main must block for one real RTOS tick so IDLE0 can service the task watchdog",
    )
    require("CONFIG_SHAHBAZ_ACTUATORS_ENABLE=n" in defaults,
            "default build must keep physical actuators disabled")
    require(
        re.search(
            r"set\(SHAHBAZ_ACTUATOR_BACKEND\s+\"null\"\s+CACHE\s+STRING",
            root_cmake,
        ) is not None and
        "SHAHBAZ_ACTUATOR_BACKEND must be 'null' or 'espidf'" in root_cmake,
        "root build must default to the null backend and reject unknown actuator profiles",
    )
    require("CONFIG_BT_ENABLED" not in defaults,
            "trimmed sdkconfig.defaults must not set unavailable Bluetooth Kconfig symbols")
    require(
        re.search(r"config\s+SHAHBAZ_ACTUATORS_ENABLE\b", main_kconfig) is not None and
        "default n" in main_kconfig,
        "always-seeded main/Kconfig.projbuild must declare the fail-closed actuator selector",
    )
    require(
        re.search(r"config\s+SHAHBAZ_ACTUATORS_ENABLE\b", actuator_kconfig) is None,
        "optional actuator component must not redeclare the global actuator selector",
    )
    require(
        "set(SHAHBAZ_ACTUATOR_COMPONENTS actuator_null)" in main_cmake and
        "list(APPEND SHAHBAZ_ACTUATOR_COMPONENTS actuator_espidf)" in main_cmake and
        "${SHAHBAZ_ACTUATOR_COMPONENTS}" in main_cmake and
        "SHAHBAZ_BUILD_PHYSICAL_ACTUATORS=1" in main_cmake and
        "CONFIG_SHAHBAZ_ACTUATORS_ENABLE" not in main_cmake,
        "main must select actuator dependencies from the explicit pre-Kconfig build profile",
    )
    require(
        "null_actuator_controller.hpp" in app_main and
        "espidf_pwm_actuator_controller.hpp" in app_main and
        "EspIdfPwmActuatorController" in app_main and
        "SHAHBAZ_BUILD_PHYSICAL_ACTUATORS" in app_main and
        "requires -DSHAHBAZ_ACTUATOR_BACKEND=espidf" in app_main and
        "CONFIG_SHAHBAZ_ACTUATORS_ENABLE" in app_main and
        "actuator_output_gate_open(board_report)" in app_main and
        "active_actuator = &pwm_actuator" in app_main and
        "active_actuator = &null_actuator" in app_main,
        "production composition must default to null and gate the physical actuator backend",
    )
    safety_fallback_ms = int_literal(
        safety_kconfig,
        r"config\s+SHAHBAZ_HEARTBEAT_TIMEOUT_MS\b.*?\bdefault\s+([0-9]+)",
        "heartbeat timeout Kconfig fallback",
    )
    require(safety_fallback_ms == 0,
            "heartbeat timeout component fallback must remain fail-closed at 0 ms")
    require(cfg_int(default_config, "CONFIG_SHAHBAZ_HEARTBEAT_TIMEOUT_MS") == 1000,
            "production project default must select the reviewed 1000 ms heartbeat timeout")
    control_fallback_ms = int_literal(
        safety_kconfig,
        r"config\s+SHAHBAZ_CONTROL_COMMAND_TIMEOUT_MS\b.*?\bdefault\s+([0-9]+)",
        "control-command timeout Kconfig fallback",
    )
    require(control_fallback_ms == 0,
            "control-command timeout component fallback must remain fail-closed at 0 ms")
    require(cfg_int(default_config, "CONFIG_SHAHBAZ_CONTROL_COMMAND_TIMEOUT_MS") == 250,
            "production project default must select the reviewed 250 ms control-command timeout")
    require(cfg_true(default_config, "CONFIG_ESP_CONSOLE_UART_DEFAULT") and
            cfg_true(default_config, "CONFIG_ESP_CONSOLE_SECONDARY_NONE") and
            not cfg_true(default_config, "CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG"),
            "production defaults must keep UART0 diagnostics and disable the secondary USB Serial/JTAG console")

    # Product architecture contract: Android + Shahbaz is operational; Windows is HIL only.
    intended = str(native_usb.get("intended_role", ""))
    require("Android phone running Shahbaz application" in intended,
            "native USB profile must name Android + Shahbaz as the operational host")
    require("Windows" in intended and "development HIL" in intended,
            "native USB profile must restrict Windows to development/HIL role")
    require(native_usb.get("product_component_name") == "shahbaz_interface_board",
            "product component identity must be shahbaz_interface_board")
    android_transport = text("android_reference/src/android/kotlin/com/shahbaz/androidusb/ShahbazUsbCdcTransport.kt")
    android_permission = text("android_reference/src/android/kotlin/com/shahbaz/androidusb/ShahbazUsbPermission.kt")
    android_client = text("android_reference/src/android/kotlin/com/shahbaz/androidusb/ShahbazInterfaceBoardClient.kt")
    android_session = text("android_reference/src/main/kotlin/com/shahbaz/protocol/ShahbazLinkSession.kt")
    require("UsbManager" in android_transport and "bulkTransfer" in android_transport and
            "USB_CLASS_CDC_DATA" in android_transport,
            "Android UsbManager CDC bulk transport implementation is missing/incomplete")
    require("requestPermission" in android_permission and "EXTRA_PERMISSION_GRANTED" in android_permission,
            "Android USB permission request/result implementation is missing")
    require("ShahbazLinkSession" in android_client and "SystemClock.elapsedRealtimeNanos" in android_client,
            "Android Shahbaz interface-board client/session composition is missing")
    require("TIME_SYNC_REFRESH_US" in android_session and "onUsbDetached" in android_session and
            "sessionToken = null" in android_session,
            "Android operational session does not enforce periodic TimeSync/reconnect reset")
    require((ROOT / "doc/English/07_ANDROID_SHAHBAZ_INTEGRATION_TEST.en.md").is_file(),
            "Android + Shahbaz integration acceptance document is missing")
    acceptance = text("doc/English/08_COMPLETE_SYSTEM_ACCEPTANCE.en.md")
    require("Android + Shahbaz operational integration" in acceptance and
            "Windows HIL remains a supporting development/diagnostic result" in acceptance,
            "complete-system acceptance must require Android + Shahbaz and demote Windows HIL")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--sdkconfig", type=Path,
                        help="ESP-IDF sdkconfig to validate production safety/evidence authorization")
    parser.add_argument("--actuator-backend", choices=("null", "espidf"), default="null",
                        help="configured actuator component profile (default: null)")
    args = parser.parse_args(argv)
    try:
        validate_contract()
        validate_evidence_guard_self_test()
        validate_production_component_seed_self_test()
        validate_trimmed_config_self_test()
        if args.sdkconfig is not None:
            validate_build_config(parse_sdkconfig(args.sdkconfig), args.actuator_backend)
    except Exception as exc:  # malformed manifests must fail closed
        fail(f"contract validator exception: {exc}")

    for message in ERRORS:
        print(f"[FAIL] {message}", file=sys.stderr)
    if ERRORS:
        print(f"Firmware/hardware contract validation failed with {len(ERRORS)} error(s).")
        return 1
    suffix = " + build/evidence authorization" if args.sdkconfig is not None else ""
    print(f"[PASS] firmware/hardware/product semantic contract{suffix}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
