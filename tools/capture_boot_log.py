#!/usr/bin/env python3
"""Capture and validate a bounded ESP32 production UART boot/health transcript."""
from __future__ import annotations

import argparse
from datetime import datetime
from pathlib import Path
import sys
import time


REQUIRED_PRODUCTION_MARKERS = (
    "boot: ESP-IDF v5.4.4 2nd stage bootloader",
    "SPI Mode       : QIO",
    "SPI Flash Size : 16MB",
    "esp_psram: Found 8MB PSRAM device",
    "esp_psram: SPI SRAM memory test OK",
    "app_init: ESP-IDF:          v5.4.4",
    "PWM actuators excluded from this production image",
    "I2C ready: SDA=8 SCL=9 400kHz",
    "native USB CDC initialization: ready",
    "app_main subscribed to ESP task watchdog",
    "SHT30=online",
    "MS5611=online",
)

FORBIDDEN_RUNTIME_MARKERS = (
    "Task watchdog got triggered",
    "Guru Meditation Error",
    "rst:0xc (RTC_SW_CPU_RST)",
    "abort() was called",
    "assert failed:",
)


def validate_production_transcript(transcript: str) -> list[str]:
    errors: list[str] = []
    for marker in REQUIRED_PRODUCTION_MARKERS:
        if marker not in transcript:
            errors.append(f"missing production boot/health marker: {marker}")
    if "state=" not in transcript:
        errors.append("missing periodic runtime status line")
    for marker in FORBIDDEN_RUNTIME_MARKERS:
        if marker in transcript:
            errors.append(f"forbidden reset/panic marker observed: {marker}")
    return errors


def self_test() -> int:
    healthy = "\n".join(REQUIRED_PRODUCTION_MARKERS) + "\nstate=1 usb=0 SHT30=online MS5611=online\n"
    if validate_production_transcript(healthy):
        print("[FAIL] boot transcript validator rejected healthy fixture")
        return 1
    for label, transcript in (
        ("watchdog reset", healthy + "Task watchdog got triggered\nrst:0xc (RTC_SW_CPU_RST)\n"),
        ("offline sensor", healthy.replace("SHT30=online", "SHT30=offline")),
        ("missing status", "\n".join(REQUIRED_PRODUCTION_MARKERS)),
    ):
        if not validate_production_transcript(transcript):
            print(f"[FAIL] boot transcript validator accepted {label} fixture")
            return 1
    print("[PASS] production boot/reset/watchdog/sensor-health transcript self-test")
    return 0


def timestamped(line: str) -> str:
    stamp = datetime.now().astimezone().isoformat(timespec="milliseconds")
    return f"{stamp} {line}"


def capture(port: str, duration: float, reset: bool) -> str:
    try:
        import serial  # type: ignore
    except ImportError as exc:
        raise RuntimeError("pyserial 3.5 or newer is required") from exc

    ser = serial.Serial(port=None, baudrate=115200, timeout=0.05, write_timeout=1.0)
    ser.port = port
    # Match ESP-IDF monitor's inactive line state before opening the port.
    ser.rts = False
    ser.dtr = False
    ser.open()
    try:
        ser.reset_input_buffer()
        if reset:
            # CH343 RTS drives ESP32-S3 EN active-low. Keep DTR inactive so
            # GPIO0 remains high and the production application boots.
            ser.dtr = False
            ser.rts = True
            time.sleep(0.1)
            ser.rts = False

        deadline = time.monotonic() + duration
        pending = bytearray()
        output: list[str] = []
        while time.monotonic() < deadline:
            data = ser.read(4096)
            if not data:
                continue
            pending.extend(data)
            while b"\n" in pending:
                raw, _, remainder = pending.partition(b"\n")
                pending = bytearray(remainder)
                line = raw.rstrip(b"\r").decode("utf-8", errors="replace")
                rendered = timestamped(line)
                output.append(rendered)
                print(rendered, flush=True)
        if pending:
            rendered = timestamped(pending.rstrip(b"\r").decode("utf-8", errors="replace"))
            output.append(rendered)
            print(rendered, flush=True)
        return "\n".join(output) + ("\n" if output else "")
    finally:
        ser.close()


def main(argv: list[str] | None = None) -> int:
    # Windows PowerShell 5 inherits a legacy CP1252 stream even when the
    # transcript/artifact is UTF-8. TinyUSB prints a Unicode descriptor table.
    for stream in (sys.stdout, sys.stderr):
        reconfigure = getattr(stream, "reconfigure", None)
        if reconfigure is not None:
            reconfigure(encoding="utf-8", errors="backslashreplace")

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--port", help="UART/boot-log port, for example COM9")
    parser.add_argument("--duration", type=float, default=12.0,
                        help="bounded capture duration in seconds (default 12)")
    parser.add_argument("--no-reset", action="store_true",
                        help="capture the running device without toggling EN")
    parser.add_argument("--validate-production", action="store_true",
                        help="require complete production boot and online sensor health")
    parser.add_argument("--artifact", type=Path,
                        help="UTF-8 transcript artifact path")
    args = parser.parse_args(argv)

    if args.self_test:
        return self_test()
    if not args.port:
        parser.error("--port is required unless --self-test is used")
    if args.duration <= 0:
        parser.error("--duration must be positive")
    if args.validate_production and (args.no_reset or args.duration < 7.0):
        parser.error("production validation requires reset capture lasting at least 7 seconds")

    transcript = capture(args.port, args.duration, not args.no_reset)
    if args.artifact:
        args.artifact.parent.mkdir(parents=True, exist_ok=True)
        args.artifact.write_text(transcript, encoding="utf-8")
        print(f"[ARTIFACT] {args.artifact.resolve()}")
    if args.validate_production:
        errors = validate_production_transcript(transcript)
        for error in errors:
            print(f"[FAIL] {error}", file=sys.stderr)
        if errors:
            return 1
        print("[PASS] production boot, watchdog stability, and dual-sensor health")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
