#!/usr/bin/env python3
"""Read-only structural safety checks for project-owned Shahbaz firmware source."""
from __future__ import annotations

import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ERRORS: list[str] = []
WARNINGS: list[str] = []
SOURCE_EXTENSIONS = {".c", ".cc", ".cpp", ".h", ".hh", ".hpp"}


def relative(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def production_sources() -> list[Path]:
    result: list[Path] = []
    for base_name in ("components", "main"):
        for path in (ROOT / base_name).rglob("*"):
            if not path.is_file() or path.suffix not in SOURCE_EXTENSIONS:
                continue
            if "test" in path.relative_to(ROOT).parts:
                continue
            result.append(path)
    return sorted(result)


def require_tokens(rel: str, tokens: tuple[str, ...]) -> None:
    path = ROOT / rel
    if not path.is_file():
        ERRORS.append(f"required file missing: {rel}")
        return
    text = path.read_text(encoding="utf-8", errors="replace")
    for token in tokens:
        if token not in text:
            ERRORS.append(f"required token {token!r} missing from {rel}")


def main() -> int:
    sources = production_sources()
    for path in sources:
        text = path.read_text(encoding="utf-8", errors="replace")
        rel = relative(path)
        if re.search(r'#\s*include\s*[<"]Arduino\.h[>"]', text):
            ERRORS.append(f"Arduino dependency: {rel}")
        if re.search(r"\b(?:malloc|calloc|realloc)\s*\(", text) or re.search(r"\bnew\s+[A-Za-z_:][A-Za-z0-9_:<>]*\s*[({]", text):
            ERRORS.append(f"dynamic allocation in project source: {rel}")
        if re.search(r"\b(?:ledc_|mcpwm_|rmt_(?:transmit|write|enable|new))[A-Za-z0-9_]*\s*\(", text):
            if not rel.startswith("components/actuator_espidf/"):
                ERRORS.append(f"actuator output API outside actuator_espidf: {rel}")
        if re.search(r"\b(?:gpio_set_level|gpio_set_direction)\s*\(", text):
            if not (rel.startswith("components/platform_espidf/") or rel.startswith("components/actuator_espidf/")):
                ERRORS.append(f"direct GPIO drive outside approved platform/actuator adapter: {rel}")

    checks = {
        "components/safety_supervisor/src/safety_supervisor.cpp": (
            "heartbeat_required_for_recovery_", "forceSafe", "handleArmRequest", "handleActuatorCommand"),
        "components/actuator_espidf/src/espidf_pwm_actuator_controller.cpp": (
            "pinConfigurationValid", "ledc_stop", "NotArmed", "HardwareError"),
        "components/usb_transport_espidf/src/espidf_usb_cdc_transport.cpp": (
            "TINYUSB_EVENT_ATTACHED", "TINYUSB_EVENT_DETACHED", "tinyusb_cdcacm_read",
            "tinyusb_cdcacm_write_queue", "resetSession", "revokeSession",
            "xQueueReceive(rx_queue_", "chunk.epoch", "sessionAdmitted"),
        "components/platform_espidf/src/espidf_i2c_bus.cpp": (
            "i2c_new_master_bus", "i2c_master_transmit_receive", "recover",
            "configurationAuthorized", "is_valid_i2c_pair"),
        "main/app_main.cpp": (
            "SharedSensorScheduler", "ProtocolEngine", "safety.evaluate(clock.now_us())", "serviceTx",
            "TaskHealthMonitor", "esp_task_wdt_add(nullptr)", "esp_task_wdt_reset()"),
        "components/device_link/src/protocol_engine.cpp": (
            "validateAndStripSessionToken", "SessionMismatch", "senderFreshness",
            "maximum_sender_age_us", "session_token_"),
        "components/actuator_espidf/Kconfig": ("SHAHBAZ_ACTUATORS_ENABLE", "default n"),
        "components/board_support/Kconfig": ("SHAHBAZ_ENABLE_NATIVE_USB_TRANSPORT", "default y"),
    }
    for rel, tokens in checks.items():
        require_tokens(rel, tokens)

    # The fallback actuator must remain independent of ESP-IDF/hardware headers.
    for path in (ROOT / "components/actuator_null").rglob("*"):
        if not path.is_file() or path.suffix not in SOURCE_EXTENSIONS:
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        if re.search(r'#\s*include\s*[<"](?:driver/|esp_|freertos/|tinyusb|tusb)', text):
            ERRORS.append(f"Null actuator has a hardware/platform dependency: {relative(path)}")

    if shutil.which("cmake") is None:
        WARNINGS.append("CMake unavailable; host tests were not executed by this script.")
    if shutil.which("idf.py") is None:
        WARNINGS.append("idf.py unavailable; target build was not executed by this script.")

    for warning in WARNINGS:
        print(f"[WARN] {warning}", file=sys.stderr)
    for error in ERRORS:
        print(f"[FAIL] {error}", file=sys.stderr)
    if ERRORS:
        print(f"Firmware structural safety check failed with {len(ERRORS)} error(s).")
        return 1
    print(f"[PASS] firmware structural safety check: {len(sources)} production source file(s)")
    print("This result does not replace compilation, unit tests, ESP-IDF target build, or physical HIL.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
