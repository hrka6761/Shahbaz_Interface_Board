#!/usr/bin/env python3
"""Verify that an ESP-IDF build contains production firmware artifacts only.

The check consumes artifacts emitted by ESP-IDF 5.4+ rather than inspecting
only the source tree:

* ``compile_commands.json`` proves which translation units were compiled and
  which target toolchain compiled them;
* ``project_description.json`` provides the resolved component graph;
* ``sdkconfig`` and ``flasher_args.json`` prove target/flash/safety settings;
* the application linker map proves which component archives reached the link;
* ELF, image, partition-table, and flash-file headers guard against stale or
  unrelated host-build artifacts; and
* ELF/image modification times must not predate any firmware source or build
  input under ``main/``, ``components/``, or ``managed_components/``.

With no build state the default command prints SKIP and succeeds, so it is safe
in host-only CI. Pass ``--require-build`` after ``idf.py build`` to fail closed
when any required target artifact is absent.
"""
from __future__ import annotations

import argparse
import csv
import json
import os
import re
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable


DEFAULT_ROOT = Path(__file__).resolve().parents[1]

EXPECTED_CONFIG = {
    "CONFIG_IDF_TARGET": "esp32s3",
    "CONFIG_ESPTOOLPY_FLASHSIZE_16MB": "y",
    "CONFIG_ESPTOOLPY_FLASHMODE_QIO": "y",
    "CONFIG_PARTITION_TABLE_CUSTOM": "y",
    "CONFIG_PARTITION_TABLE_CUSTOM_FILENAME": "partitions.csv",
    "CONFIG_SPIRAM": "y",
    "CONFIG_SPIRAM_MODE_OCT": "y",
    "CONFIG_SPIRAM_SPEED_80M": "y",
    "CONFIG_SPIRAM_BOOT_INIT": "y",
    "CONFIG_SHAHBAZ_ENABLE_NATIVE_USB_TRANSPORT": "y",
    "CONFIG_TINYUSB_CDC_ENABLED": "y",
    "CONFIG_TINYUSB_CDC_COUNT": "1",
    "CONFIG_ESP_CONSOLE_UART_DEFAULT": "y",
    "CONFIG_ESP_CONSOLE_SECONDARY_NONE": "y",
    "CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG": "n",
    "CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED": "n",
    "CONFIG_SHAHBAZ_HEARTBEAT_TIMEOUT_MS": "1000",
    "CONFIG_SHAHBAZ_I2C_SDA_GPIO": "8",
    "CONFIG_SHAHBAZ_I2C_SCL_GPIO": "9",
    "CONFIG_ESP_TASK_WDT_EN": "y",
    "CONFIG_ESP_TASK_WDT_INIT": "y",
    "CONFIG_ESP_TASK_WDT_PANIC": "y",
    "CONFIG_ESP_TASK_WDT_TIMEOUT_S": "2",
    "CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0": "y",
    "CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1": "y",
    "CONFIG_FREERTOS_HZ": "1000",
}

# ESP-IDF omits some disabled, derived symbols from sdkconfig entirely instead
# of serializing them as "# ... is not set". Only these explicitly listed
# derived symbols may use absence as the fail-closed "n" representation.
OMITTED_MEANS_DISABLED_CONFIG = {
    "CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED",
}

PROJECT_SOURCE_ROOTS = {"main", "components", "managed_components"}
FORBIDDEN_PROJECT_ROOTS = {
    ".github",
    ".gradle",
    ".idea",
    "android_reference",
    "artifacts",
    "build-host",
    "ci",
    "doc",
    "docs",
    "hardware_reference",
    "test",
    "tests",
    "tools",
}
FORBIDDEN_PATH_SEGMENTS = {
    "android",
    "android_reference",
    "androidtest",
    "ci",
    "doc",
    "docs",
    "fixture",
    "fixtures",
    "hil",
    "kotlin",
    "python",
    "test",
    "testdata",
    "tests",
    "tools",
}
FORBIDDEN_COMPONENT_PATTERN = re.compile(
    r"(?:^|[_\-:])(?:android|catch2|ci|cmock|docs?|doctest|fixture|fixtures|"
    r"gmock|googletest|gtest|hil|kotlin|pytest|python|test|tests|test_utils|"
    r"testing|tools|unity)(?:$|[_\-:])",
    re.IGNORECASE,
)
FORBIDDEN_ARCHIVES = {
    "libbt.a",
    "libcatch2.a",
    "libcmock.a",
    "libdoctest.a",
    "libgmock.a",
    "libgoogletest.a",
    "libgtest.a",
    "libpytest.a",
    "libtest_utils.a",
    "libunity.a",
}
FORBIDDEN_ASSET_SUFFIXES = {
    ".bat",
    ".bmp",
    ".cmd",
    ".csv",
    ".gif",
    ".html",
    ".java",
    ".jpeg",
    ".jpg",
    ".json",
    ".kt",
    ".kts",
    ".md",
    ".markdown",
    ".pdf",
    ".png",
    ".ps1",
    ".psd1",
    ".psm1",
    ".py",
    ".pyc",
    ".rst",
    ".svg",
    ".xml",
    ".yaml",
    ".yml",
}
COMPILED_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".s", ".asm"}
FIRMWARE_INPUT_SUFFIXES = COMPILED_SUFFIXES | {
    ".cmake",
    ".csv",
    ".h",
    ".hh",
    ".hpp",
    ".inc",
    ".ld",
    ".lld",
    ".py",
    ".yaml",
    ".yml",
}
FIRMWARE_ROOT_INPUT_NAMES = {
    "CMakeLists.txt",
    "Kconfig",
    "Kconfig.projbuild",
    "dependencies.lock",
    "idf_component.yml",
    "partitions.csv",
    "sdkconfig",
    "sdkconfig.defaults",
}
ASSET_SUFFIX_PATTERN = re.compile(
    r"\.(?:bat|bmp|cmd|csv|gif|html|java|jpeg|jpg|json|kt|kts|md|markdown|"
    r"pdf|png|ps1|psd1|psm1|py|pyc|rst|svg|xml|yaml|yml)(?:\b|$)",
    re.IGNORECASE,
)
ARCHIVE_PATTERN = re.compile(r"\blib[A-Za-z0-9_.+\-]+\.a\b", re.IGNORECASE)


def canonical(path: Path) -> str:
    return os.path.normcase(os.path.abspath(os.fspath(path)))


def relative_to(path: Path, parent: Path) -> Path | None:
    try:
        return Path(canonical(path)).relative_to(Path(canonical(parent)))
    except ValueError:
        return None


def same_path(left: Path, right: Path) -> bool:
    return canonical(left) == canonical(right)


def metadata_path(raw: Any, base: Path) -> Path | None:
    if not isinstance(raw, str) or not raw.strip():
        return None
    candidate = Path(raw.strip().strip('"'))
    if not candidate.is_absolute():
        candidate = base / candidate
    return Path(os.path.abspath(candidate))


def parse_sdkconfig(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    not_set = re.compile(r"^#\s*(CONFIG_[A-Za-z0-9_]+)\s+is not set$")
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw.strip()
        match = not_set.match(line)
        if match:
            values[match.group(1)] = "n"
        elif line.startswith("CONFIG_") and "=" in line:
            key, value = line.split("=", 1)
            values[key.strip()] = value.strip().strip('"')
    return values


def json_list(value: Any) -> list[str]:
    if isinstance(value, list):
        return [str(item) for item in value]
    if isinstance(value, str):
        return [item for item in value.split(";") if item]
    return []


def parse_size(value: str) -> int:
    cleaned = value.strip().upper()
    multiplier = 1
    if cleaned.endswith("K"):
        multiplier, cleaned = 1024, cleaned[:-1]
    elif cleaned.endswith("M"):
        multiplier, cleaned = 1024 * 1024, cleaned[:-1]
    return int(cleaned, 0) * multiplier


def normalized_component(value: str) -> str:
    value = value.lower().replace("-", "_")
    if "::" in value:
        value = value.rsplit("::", 1)[-1]
    return value


@dataclass
class Result:
    skipped: bool = False
    errors: list[str] = field(default_factory=list)
    warnings: list[str] = field(default_factory=list)
    graph_components: list[str] = field(default_factory=list)
    linked_components: list[str] = field(default_factory=list)
    linked_archives: list[str] = field(default_factory=list)
    idf_version: str = "unknown"
    target: str = "unknown"
    app_binary: Path | None = None
    app_size: int | None = None
    app_partition_size: int | None = None
    app_partition_offset: int | None = None
    map_file: Path | None = None

    @property
    def passed(self) -> bool:
        return not self.skipped and not self.errors


class ProductionBuildVerifier:
    def __init__(self, root: Path, build_dir: Path, require_build: bool = False):
        self.root = Path(os.path.abspath(root))
        self.build_dir = Path(os.path.abspath(build_dir))
        self.require_build = require_build
        self.result = Result()
        self.description: dict[str, Any] = {}
        self.component_paths: dict[str, Path] = {}
        self.sdkconfig_values: dict[str, str] = {}
        self.elf_path: Path | None = None
        self.idf_path: Path | None = None

    def error(self, message: str) -> None:
        if message not in self.result.errors:
            self.result.errors.append(message)

    def warning(self, message: str) -> None:
        if message not in self.result.warnings:
            self.result.warnings.append(message)

    def read_json(self, path: Path, label: str) -> Any:
        try:
            return json.loads(path.read_text(encoding="utf-8"))
        except (OSError, UnicodeError, json.JSONDecodeError) as exc:
            self.error(f"cannot read {label} {path}: {exc}")
            return None

    def verify(self) -> Result:
        build_markers = (
            "CMakeCache.txt",
            "build.ninja",
            "compile_commands.json",
            "flasher_args.json",
            "project_description.json",
        )
        present = self.build_dir.is_dir() and any(
            (self.build_dir / marker).exists() for marker in build_markers
        )
        if not present:
            if self.require_build:
                self.error(f"ESP-IDF build artifacts are absent from {self.build_dir}")
            else:
                self.result.skipped = True
            return self.result

        required = {
            "compile database": self.build_dir / "compile_commands.json",
            "component graph": self.build_dir / "project_description.json",
            "flash manifest": self.build_dir / "flasher_args.json",
        }
        for label, path in required.items():
            if not path.is_file():
                self.error(f"required {label} is missing: {path}")
        if self.result.errors:
            return self.result

        description = self.read_json(required["component graph"], "component graph")
        if not isinstance(description, dict):
            return self.result
        self.description = description

        self.verify_description()
        self.verify_sdkconfig()
        self.verify_component_graph()
        self.verify_compile_commands(required["compile database"])
        self.verify_images()
        self.verify_artifact_freshness()
        self.verify_flash_manifest(required["flash manifest"])
        self.verify_linker_map()
        return self.result

    def verify_description(self) -> None:
        project_name = self.description.get("project_name")
        if project_name != "shahbaz_sensor_usb_node":
            self.error(
                "component graph belongs to unexpected project "
                f"{project_name!r}, expected 'shahbaz_sensor_usb_node'"
            )

        described_root = metadata_path(self.description.get("project_path"), self.build_dir)
        if described_root is None or not same_path(described_root, self.root):
            self.error(
                f"component graph project_path does not match project root: {described_root}"
            )
        described_build = metadata_path(self.description.get("build_dir"), self.build_dir)
        if described_build is not None and not same_path(described_build, self.build_dir):
            self.error(
                f"component graph build_dir does not match inspected build: {described_build}"
            )

        target = str(self.description.get("target", ""))
        self.result.target = target or "unknown"
        if target != "esp32s3":
            self.error(f"component graph target is {target!r}, expected 'esp32s3'")

        self.idf_path = metadata_path(self.description.get("idf_path"), self.build_dir)
        if self.idf_path is None or not self.idf_path.is_dir():
            self.error(f"component graph identifies an invalid ESP-IDF path: {self.idf_path}")

        git_revision = str(self.description.get("git_revision", ""))
        if git_revision != "v5.4.4":
            self.error(
                f"component graph ESP-IDF revision is {git_revision!r}, expected 'v5.4.4'"
            )

        version_candidates = (
            self.description.get("git_revision"),
            self.description.get("idf_ver"),
            self.description.get("idf_version"),
            self.description.get("idf_path"),
        )
        for candidate in version_candidates:
            match = re.search(r"(?:^|[^0-9])(\d+)\.(\d+)(?:\.(\d+))?", str(candidate or ""))
            if match:
                self.result.idf_version = match.group(0).lstrip("vV-_ ")
                major, minor = int(match.group(1)), int(match.group(2))
                if (major, minor) < (5, 4):
                    self.error(
                        f"ESP-IDF {self.result.idf_version} is older than required 5.4"
                    )
                break
        if self.result.idf_version == "unknown":
            self.error("component graph does not identify an ESP-IDF version")

        self.elf_path = metadata_path(self.description.get("app_elf"), self.build_dir)
        self.result.app_binary = metadata_path(self.description.get("app_bin"), self.build_dir)
        if self.elf_path is None:
            self.error("component graph does not identify app_elf")
        if self.result.app_binary is None:
            self.error("component graph does not identify app_bin")

    def component_graph_entries(self) -> dict[str, Path]:
        entries: dict[str, Path] = {}
        names = json_list(self.description.get("build_components"))
        paths = json_list(self.description.get("build_component_paths"))
        if names and len(names) != len(paths):
            self.error(
                "component graph build_components/build_component_paths lengths differ: "
                f"{len(names)} != {len(paths)}"
            )
        for name, raw_path in zip(names, paths):
            path = metadata_path(raw_path, self.root)
            if path is not None:
                entries[name] = path

        info = self.description.get("build_component_info")
        if isinstance(info, dict):
            for name, details in info.items():
                if not isinstance(details, dict):
                    continue
                raw_path = details.get("dir") or details.get("path") or details.get("component_dir")
                path = metadata_path(raw_path, self.root)
                if path is not None:
                    entries.setdefault(str(name), path)
        return entries

    def verify_component_graph(self) -> None:
        self.component_paths = self.component_graph_entries()
        self.result.graph_components = sorted(self.component_paths, key=str.lower)
        if not self.component_paths:
            self.error("component graph contains no component path information")
            return
        if not any(normalized_component(name) == "main" for name in self.component_paths):
            self.error("component graph does not contain the production main component")

        normalized_components = {
            normalized_component(name) for name in self.component_paths
        }
        if "bt" in normalized_components:
            self.error("Bluetooth component is in the trimmed sensor/USB production graph: bt")
        if self.sdkconfig_values.get("CONFIG_SHAHBAZ_ACTUATORS_ENABLE", "n") == "n":
            if "actuator_null" not in normalized_components:
                self.error("disabled-actuator production graph is missing actuator_null")
            for component in ("actuator_espidf", "esp_driver_ledc"):
                if component in normalized_components:
                    self.error(
                        "disabled-actuator production graph contains forbidden component: "
                        f"{component}"
                    )

        for name, path in sorted(self.component_paths.items()):
            normalized_name = normalized_component(name)
            if FORBIDDEN_COMPONENT_PATTERN.search(normalized_name):
                self.error(f"test/development component is in build graph: {name} ({path})")
            idf_rel = relative_to(path, self.idf_path) if self.idf_path is not None else None
            if idf_rel is not None:
                if any(part.lower() in FORBIDDEN_PATH_SEGMENTS for part in idf_rel.parts):
                    self.error(f"test/fixture ESP-IDF component is in build graph: {name} ({path})")
                continue
            rel = relative_to(path, self.root)
            if rel is None:
                if any(part.lower() in FORBIDDEN_PATH_SEGMENTS for part in path.parts):
                    self.error(f"test/fixture component path is in build graph: {name} ({path})")
                continue
            if not rel.parts:
                self.error(f"project root was registered as a component: {name}")
                continue
            parts = tuple(part.lower() for part in rel.parts)
            if parts[0] not in PROJECT_SOURCE_ROOTS:
                self.error(f"non-production project path is in component graph: {name} ({rel})")
            if any(part in FORBIDDEN_PATH_SEGMENTS for part in parts):
                self.error(f"test/fixture project component is in build graph: {name} ({rel})")

    def verify_sdkconfig(self) -> None:
        config_path = metadata_path(self.description.get("config_file"), self.build_dir)
        if config_path is None:
            config_path = self.root / "sdkconfig"
        if not config_path.is_file():
            self.error(f"configured sdkconfig is missing: {config_path}")
            return
        if not same_path(config_path, self.root / "sdkconfig"):
            self.error(f"build used unexpected sdkconfig outside project root: {config_path}")
        raw_config = config_path.read_text(encoding="utf-8", errors="replace")
        values = parse_sdkconfig(config_path)
        self.sdkconfig_values = values
        for key, expected in EXPECTED_CONFIG.items():
            actual = values.get(key)
            if actual is None and expected == "n" and key in OMITTED_MEANS_DISABLED_CONFIG:
                actual = "n"
            if actual != expected:
                self.error(f"sdkconfig invariant {key}={actual!r}, expected {expected!r}")
        if values.get("CONFIG_SHAHBAZ_ACTUATORS_ENABLE", "n") != "n":
            self.error("sensor/USB production sdkconfig enables physical actuators")
        if re.search(r"(?<![A-Za-z0-9_])CONFIG_BT_ENABLED(?![A-Za-z0-9_])", raw_config):
            self.error(
                "trimmed production sdkconfig must not contain unavailable CONFIG_BT_ENABLED"
            )

    def forbidden_project_path(self, path: Path) -> str | None:
        idf_rel = relative_to(path, self.idf_path) if self.idf_path is not None else None
        if idf_rel is not None:
            parts = tuple(part.lower() for part in idf_rel.parts)
            if any(part in FORBIDDEN_PATH_SEGMENTS for part in parts):
                return "ESP-IDF source is under a test/development path"
            if path.suffix.lower() not in COMPILED_SUFFIXES:
                return f"ESP-IDF source has non-firmware suffix {path.suffix!r}"
            return None
        rel = relative_to(path, self.root)
        if rel is None:
            if path.suffix.lower() in FORBIDDEN_ASSET_SUFFIXES:
                return f"external source has non-firmware suffix {path.suffix!r}"
            if any(part.lower() in FORBIDDEN_PATH_SEGMENTS for part in path.parts):
                return "external source is under a test/development path"
            return None
        build_rel = relative_to(path, self.build_dir)
        parts = tuple(part.lower() for part in rel.parts)
        if build_rel is not None:
            if any(part in FORBIDDEN_PATH_SEGMENTS for part in parts):
                return "generated target source has a test/fixture path"
            if path.suffix.lower() not in COMPILED_SUFFIXES:
                return f"generated target source has non-code suffix {path.suffix!r}"
            return None
        if not parts or parts[0] not in PROJECT_SOURCE_ROOTS:
            return "source is outside main/components/managed_components"
        if any(part in FORBIDDEN_PATH_SEGMENTS for part in parts):
            return "source is under a test/fixture directory"
        stem = path.stem.lower()
        if stem.startswith("test_") or stem.endswith("_test") or ".test." in path.name.lower():
            return "source filename is test-only"
        if path.suffix.lower() not in COMPILED_SUFFIXES:
            return f"source has non-firmware suffix {path.suffix!r}"
        return None

    def forbidden_reference(self, text: str) -> str | None:
        normalized = text.replace("\\", "/").lower()
        root = str(self.root).replace("\\", "/").lower().rstrip("/")
        if root not in normalized:
            return None
        for top in FORBIDDEN_PROJECT_ROOTS:
            marker = f"{root}/{top.lower()}"
            if marker in normalized:
                return f"references project development path {top}/"
        component_test = re.compile(
            re.escape(root) + r"/(?:components|managed_components)/[^\s\"']+/(?:test|tests|fixtures)(?:/|\b)"
        )
        if component_test.search(normalized):
            return "references a component test/fixture path"
        if ASSET_SUFFIX_PATTERN.search(normalized):
            return "references a non-firmware asset/script suffix"
        return None

    def verify_compile_commands(self, path: Path) -> None:
        database = self.read_json(path, "compile database")
        if not isinstance(database, list) or not database:
            self.error("compile_commands.json is empty or not a JSON list")
            return
        target_commands = 0
        for index, entry in enumerate(database):
            if not isinstance(entry, dict):
                self.error(f"compile command entry {index} is not an object")
                continue
            directory = metadata_path(entry.get("directory"), self.build_dir) or self.build_dir
            source = metadata_path(entry.get("file"), directory)
            if source is None:
                self.error(f"compile command entry {index} has no source file")
                continue
            reason = self.forbidden_project_path(source)
            if reason:
                self.error(f"forbidden compile source {source}: {reason}")

            command = entry.get("command")
            if not isinstance(command, str):
                arguments = entry.get("arguments")
                command = " ".join(str(item) for item in arguments) if isinstance(arguments, list) else ""
            if "xtensa-esp32s3-elf-" in command.lower():
                target_commands += 1
            else:
                self.error(f"compile command for {source} does not use ESP32-S3 target toolchain")
            reference_reason = self.forbidden_reference(command)
            if reference_reason:
                self.error(f"compile command for {source} {reference_reason}")
        if target_commands == 0:
            self.error("compile database contains no ESP32-S3 target compiler invocation")

    def verify_binary_header(self, path: Path | None, label: str, magic: bytes) -> None:
        if path is None:
            return
        if not path.is_file():
            self.error(f"{label} is missing: {path}")
            return
        try:
            prefix = path.read_bytes()[: len(magic)]
        except OSError as exc:
            self.error(f"cannot read {label} {path}: {exc}")
            return
        if prefix != magic:
            self.error(f"{label} has unexpected header {prefix.hex()}, expected {magic.hex()}")

    def verify_images(self) -> None:
        self.verify_binary_header(self.elf_path, "application ELF", b"\x7fELF")
        self.verify_binary_header(self.result.app_binary, "application image", b"\xe9")
        if self.result.app_binary is not None and self.result.app_binary.is_file():
            self.result.app_size = self.result.app_binary.stat().st_size

    def firmware_inputs(self) -> list[Path]:
        inputs: list[Path] = []
        for name in FIRMWARE_ROOT_INPUT_NAMES:
            candidate = self.root / name
            if candidate.is_file():
                inputs.append(candidate)
        for source_root_name in PROJECT_SOURCE_ROOTS:
            source_root = self.root / source_root_name
            if not source_root.is_dir():
                continue
            for candidate in source_root.rglob("*"):
                if not candidate.is_file():
                    continue
                if (
                    candidate.name in FIRMWARE_ROOT_INPUT_NAMES or
                    candidate.name.startswith("Kconfig") or
                    candidate.suffix.lower() in FIRMWARE_INPUT_SUFFIXES
                ):
                    inputs.append(candidate)
        return inputs

    def verify_artifact_freshness(self) -> None:
        newest_input: Path | None = None
        newest_input_mtime_ns = -1
        for candidate in self.firmware_inputs():
            try:
                candidate_mtime_ns = candidate.stat().st_mtime_ns
            except OSError as exc:
                self.error(f"cannot stat firmware input {candidate}: {exc}")
                continue
            if candidate_mtime_ns > newest_input_mtime_ns:
                newest_input = candidate
                newest_input_mtime_ns = candidate_mtime_ns
        if newest_input is None:
            self.error("project contains no firmware inputs for build-freshness verification")
            return

        for label, artifact in (
            ("application ELF", self.elf_path),
            ("application image", self.result.app_binary),
        ):
            if artifact is None or not artifact.is_file():
                continue
            try:
                artifact_mtime_ns = artifact.stat().st_mtime_ns
            except OSError as exc:
                self.error(f"cannot stat {label} {artifact}: {exc}")
                continue
            if artifact_mtime_ns < newest_input_mtime_ns:
                self.error(
                    f"{label} predates firmware input {newest_input}; "
                    "run a clean ESP-IDF build before flashing"
                )

    def partition_info(self) -> tuple[int, int] | None:
        path = self.root / "partitions.csv"
        if not path.is_file():
            self.error(f"custom partition CSV is missing: {path}")
            return None
        try:
            with path.open("r", encoding="utf-8", newline="") as stream:
                rows = list(csv.reader(line for line in stream if not line.lstrip().startswith("#")))
        except (OSError, csv.Error, ValueError) as exc:
            self.error(f"cannot parse partition CSV {path}: {exc}")
            return None
        for row in rows:
            fields = [field.strip() for field in row]
            if len(fields) >= 5 and fields[0] == "factory" and fields[1] == "app" and fields[2] == "factory":
                try:
                    return parse_size(fields[3]), parse_size(fields[4])
                except ValueError as exc:
                    self.error(f"invalid factory partition offset/size: {exc}")
                    return None
        self.error("partition CSV has no factory app partition")
        return None

    def verify_flash_manifest(self, path: Path) -> None:
        manifest = self.read_json(path, "flash manifest")
        if not isinstance(manifest, dict):
            return
        settings = manifest.get("flash_settings")
        if not isinstance(settings, dict):
            self.error("flasher_args.json has no flash_settings object")
            settings = {}
        mode = str(settings.get("flash_mode", settings.get("mode", ""))).lower()
        size = str(settings.get("flash_size", settings.get("size", ""))).lower()
        # ESP-IDF 5.4 deliberately emits a DIO-compatible boot header for
        # CONFIG_ESPTOOLPY_FLASHMODE_QIO. The bootloader detects the flash and
        # switches it to quad mode at runtime.
        if mode != "dio":
            self.error(
                f"flash manifest boot-header mode is {mode!r}, expected 'dio' for runtime QIO"
            )
        if size not in {"16mb", "16m"}:
            self.error(f"flash manifest size is {size!r}, expected '16MB'")

        flash_files = manifest.get("flash_files")
        if not isinstance(flash_files, dict) or not flash_files:
            self.error("flasher_args.json has no flash_files mapping")
            return
        resolved: dict[int, Path] = {}
        for raw_offset, raw_file in flash_files.items():
            try:
                offset = int(str(raw_offset), 0)
            except ValueError:
                self.error(f"invalid flash offset {raw_offset!r}")
                continue
            target = metadata_path(raw_file, self.build_dir)
            if target is None:
                self.error(f"invalid flash file at offset {raw_offset}: {raw_file!r}")
                continue
            resolved[offset] = target
            reason = self.forbidden_reference(str(target))
            if reason:
                self.error(f"flash payload {target} {reason}")
            if target.suffix.lower() != ".bin":
                self.error(f"non-binary payload is scheduled for flash: {target}")
            if not target.is_file() or target.stat().st_size == 0:
                self.error(f"flash payload is missing or empty: {target}")

        bootloader = resolved.get(0)
        self.verify_binary_header(bootloader, "bootloader image", b"\xe9")
        partition_binary = resolved.get(0x8000)
        self.verify_binary_header(partition_binary, "partition-table image", b"\xaa\x50")

        partition = self.partition_info()
        if partition is None:
            return
        app_offset, app_partition_size = partition
        self.result.app_partition_offset = app_offset
        self.result.app_partition_size = app_partition_size
        flashed_app = resolved.get(app_offset)
        if flashed_app is None:
            self.error(f"no application image is scheduled at factory offset 0x{app_offset:x}")
        elif self.result.app_binary is not None and not same_path(flashed_app, self.result.app_binary):
            self.error(
                f"flash manifest app {flashed_app} differs from component graph app {self.result.app_binary}"
            )
        if self.result.app_size is not None and self.result.app_size > app_partition_size:
            self.error(
                f"application image ({self.result.app_size} bytes) exceeds factory partition "
                f"({app_partition_size} bytes)"
            )

    def locate_map(self) -> Path | None:
        candidates: list[Path] = []
        if self.elf_path is not None:
            candidates.append(self.elf_path.with_suffix(".map"))
        project_name = self.description.get("project_name")
        if isinstance(project_name, str):
            candidates.append(self.build_dir / f"{project_name}.map")
        candidates.extend(sorted(self.build_dir.glob("*.map")))
        seen: set[str] = set()
        for candidate in candidates:
            key = canonical(candidate)
            if key not in seen and candidate.is_file():
                return candidate
            seen.add(key)
        return None

    def verify_linker_map(self) -> None:
        map_path = self.locate_map()
        self.result.map_file = map_path
        if map_path is None:
            self.error(f"application linker map is missing from {self.build_dir}")
            return
        try:
            contents = map_path.read_text(encoding="utf-8", errors="replace")
        except OSError as exc:
            self.error(f"cannot read linker map {map_path}: {exc}")
            return
        if not contents.strip():
            self.error(f"application linker map is empty: {map_path}")
            return

        for line_number, line in enumerate(contents.splitlines(), 1):
            reason = self.forbidden_reference(line)
            if reason:
                self.error(f"linker map line {line_number} {reason}: {line.strip()}")
            elif ASSET_SUFFIX_PATTERN.search(line) and ("LOAD" in line or ".o" in line or ".a" in line):
                self.error(
                    f"linker map line {line_number} references a script/document/asset: {line.strip()}"
                )

        archives = sorted({match.group(0).lower() for match in ARCHIVE_PATTERN.finditer(contents)})
        self.result.linked_archives = archives
        if not archives:
            self.error("linker map contains no linked component archives")
            return
        forbidden = sorted(set(archives) & FORBIDDEN_ARCHIVES)
        for archive in forbidden:
            self.error(f"test framework archive reached application link: {archive}")
        if self.sdkconfig_values.get("CONFIG_SHAHBAZ_ACTUATORS_ENABLE", "n") == "n":
            for archive in ("libactuator_espidf.a", "libesp_driver_ledc.a"):
                if archive in archives:
                    self.error(
                        "disabled-actuator production archive reached application link: "
                        f"{archive}"
                    )
        for archive in archives:
            stem = archive[3:-2] if archive.startswith("lib") and archive.endswith(".a") else archive
            if FORBIDDEN_COMPONENT_PATTERN.search(stem):
                self.error(f"test/development archive reached application link: {archive}")

        archive_components = {
            archive[3:-2].replace("-", "_") for archive in archives
            if archive.startswith("lib") and archive.endswith(".a")
        }
        linked: list[str] = []
        for name in self.result.graph_components:
            normalized = normalized_component(name)
            candidates = {normalized}
            if "__" in normalized:
                candidates.add(normalized.rsplit("__", 1)[-1])
            if candidates & archive_components:
                linked.append(name)
        self.result.linked_components = sorted(linked, key=str.lower)
        if not any(normalized_component(name) == "main" for name in linked):
            self.error("production main component archive is absent from linker map")


def print_result(result: Result, build_dir: Path) -> None:
    for warning in result.warnings:
        print(f"[WARN] {warning}", file=sys.stderr)
    for error in result.errors:
        print(f"[FAIL] {error}", file=sys.stderr)
    if result.skipped:
        print(f"[SKIP] no ESP-IDF build artifacts under {build_dir}")
        print("Run after idf.py build, or use --require-build to make absence an error.")
        return
    if result.errors:
        print(f"Production-image verification failed with {len(result.errors)} error(s).")
        return
    print("[PASS] ESP32 production image/build-artifact verification")
    print(f"ESP-IDF: {result.idf_version}; target: {result.target}")
    print(
        f"Build graph components ({len(result.graph_components)}): "
        + ", ".join(result.graph_components)
    )
    print(
        f"Linked components ({len(result.linked_components)}): "
        + ", ".join(result.linked_components)
    )
    print(
        f"Linked archives ({len(result.linked_archives)}): "
        + ", ".join(result.linked_archives)
    )
    if result.app_size is not None and result.app_partition_size is not None:
        free = result.app_partition_size - result.app_size
        usage = 100.0 * result.app_size / result.app_partition_size
        print(
            f"Application image: {result.app_size} bytes; factory partition: "
            f"{result.app_partition_size} bytes at 0x{result.app_partition_offset:x}; "
            f"usage {usage:.2f}%; free {free} bytes"
        )
    if result.map_file is not None:
        print(f"Linker map: {result.map_file}")


def write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2), encoding="utf-8")


def create_self_test_fixture(base: Path, mutate: Callable[[Path, Path], None] | None = None) -> tuple[Path, Path]:
    root = base / "project"
    build = root / "build"
    # Keep the synthetic IDF beneath the project root to cover the supported
    # project-local toolchain layout without treating it as application code.
    idf = root / ".tooling/esp-idf-v5.4.4"
    for path in (
        root / "main",
        root / "components/sensor_sht30/src",
        root / "components/actuator_null/src",
        build / "bootloader",
        build / "partition_table",
        build / "esp-idf/main",
        build / "esp-idf/sensor_sht30",
        build / "esp-idf/actuator_null",
        idf / "components/freertos",
    ):
        path.mkdir(parents=True, exist_ok=True)
    (root / "main/app_main.cpp").write_text("void app_main() {}\n", encoding="utf-8")
    (root / "components/sensor_sht30/src/sht.cpp").write_text("void sht() {}\n", encoding="utf-8")
    (root / "components/actuator_null/src/null.cpp").write_text("void null_actuator() {}\n", encoding="utf-8")
    (root / "sdkconfig").write_text(
        "\n".join(
            f"# {key} is not set" if value == "n" else f'{key}="{value}"' if key in {
                "CONFIG_IDF_TARGET", "CONFIG_PARTITION_TABLE_CUSTOM_FILENAME"
            } else f"{key}={value}"
            for key, value in EXPECTED_CONFIG.items()
            if key not in OMITTED_MEANS_DISABLED_CONFIG
        ) + "\n",
        encoding="utf-8",
    )
    (root / "partitions.csv").write_text(
        "# Name,Type,SubType,Offset,Size,Flags\n"
        "factory,app,factory,0x10000,0xC00000,\n",
        encoding="utf-8",
    )
    compile_commands = [
        {
            "directory": str(build),
            "arguments": [
                "xtensa-esp32s3-elf-g++.exe",
                "-c",
                str(root / "main/app_main.cpp"),
            ],
            "file": str(root / "main/app_main.cpp"),
            "output": str(build / "esp-idf/main/app_main.cpp.obj"),
        },
        {
            "directory": str(build),
            "command": (
                "xtensa-esp32s3-elf-g++.exe -c \""
                + str(root / "components/sensor_sht30/src/sht.cpp")
                + "\""
            ),
            "file": str(root / "components/sensor_sht30/src/sht.cpp"),
        },
    ]
    write_json(build / "compile_commands.json", compile_commands)
    write_json(
        build / "project_description.json",
        {
            "version": "1.0",
            "project_name": "shahbaz_sensor_usb_node",
            "project_path": str(root),
            "build_dir": str(build),
            "config_file": str(root / "sdkconfig"),
            "target": "esp32s3",
        "git_revision": "v5.4.4",
            "idf_path": str(idf),
            "app_elf": str(build / "shahbaz_sensor_usb_node.elf"),
            "app_bin": str(build / "shahbaz_sensor_usb_node.bin"),
            "build_components": ["main", "sensor_sht30", "actuator_null", "freertos"],
            "build_component_paths": [
                str(root / "main"),
                str(root / "components/sensor_sht30"),
                str(root / "components/actuator_null"),
                str(idf / "components/freertos"),
            ],
        },
    )
    write_json(
        build / "flasher_args.json",
        {
            "flash_settings": {"flash_mode": "dio", "flash_size": "16MB", "flash_freq": "80m"},
            "flash_files": {
                "0x0": "bootloader/bootloader.bin",
                "0x8000": "partition_table/partition-table.bin",
                "0x10000": "shahbaz_sensor_usb_node.bin",
            },
        },
    )
    (build / "CMakeCache.txt").write_text("IDF_TARGET:STRING=esp32s3\n", encoding="utf-8")
    (build / "shahbaz_sensor_usb_node.elf").write_bytes(b"\x7fELFself-test")
    (build / "shahbaz_sensor_usb_node.bin").write_bytes(b"\xe9production")
    (build / "bootloader/bootloader.bin").write_bytes(b"\xe9bootloader")
    (build / "partition_table/partition-table.bin").write_bytes(b"\xaa\x50partitions")
    (build / "shahbaz_sensor_usb_node.map").write_text(
        "LOAD " + str(build / "esp-idf/main/libmain.a") + "\n"
        "LOAD " + str(build / "esp-idf/sensor_sht30/libsensor_sht30.a") + "\n"
        "LOAD " + str(build / "esp-idf/actuator_null/libactuator_null.a") + "\n"
        "LOAD " + str(build / "esp-idf/freertos/libfreertos.a") + "\n",
        encoding="utf-8",
    )
    if mutate is not None:
        mutate(root, build)
    return root, build


def run_self_test() -> int:
    failures: list[str] = []

    def exercise(label: str, mutation: Callable[[Path, Path], None] | None,
                 predicate: Callable[[Result], bool]) -> None:
        with tempfile.TemporaryDirectory(prefix="shahbaz-production-verifier-") as temp:
            root, build = create_self_test_fixture(Path(temp), mutation)
            result = ProductionBuildVerifier(root, build, require_build=True).verify()
            if predicate(result):
                print(f"[PASS] self-test: {label}")
            else:
                failures.append(
                    f"{label}: skipped={result.skipped}, errors={result.errors}"
                )

    exercise(
        "accepts omitted disabled derived config symbol",
        None,
        lambda result: result.passed,
    )

    def enable_derived_usb_console(_root: Path, build: Path) -> None:
        config_path = build.parent / "sdkconfig"
        with config_path.open("a", encoding="utf-8") as stream:
            stream.write("CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED=y\n")

    exercise(
        "rejects enabled derived USB Serial JTAG console",
        enable_derived_usb_console,
        lambda result: any(
            "CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED='y', expected 'n'" in error
            for error in result.errors
        ),
    )

    def make_firmware_input_newer(root: Path, build: Path) -> None:
        source = root / "components/usb_transport_espidf/include/shahbaz/usb/session.hpp"
        source.parent.mkdir(parents=True, exist_ok=True)
        source.write_text("#pragma once\n", encoding="utf-8")
        newest_artifact_ns = max(
            (build / "shahbaz_sensor_usb_node.elf").stat().st_mtime_ns,
            (build / "shahbaz_sensor_usb_node.bin").stat().st_mtime_ns,
        )
        newer_ns = newest_artifact_ns + 10_000_000_000
        os.utime(source, ns=(newer_ns, newer_ns))

    exercise(
        "rejects ELF/image older than firmware source",
        make_firmware_input_newer,
        lambda result: any(
            "application ELF predates firmware input" in error for error in result.errors
        ) and any(
            "application image predates firmware input" in error for error in result.errors
        ),
    )

    def add_test_source(root: Path, build: Path) -> None:
        source = root / "components/sensor_sht30/test/sht_test.cpp"
        source.parent.mkdir(parents=True)
        source.write_text("void test_sensor() {}\n", encoding="utf-8")
        database = json.loads((build / "compile_commands.json").read_text(encoding="utf-8"))
        database.append({
            "directory": str(build),
            "command": f'xtensa-esp32s3-elf-g++.exe -c "{source}"',
            "file": str(source),
        })
        write_json(build / "compile_commands.json", database)

    exercise(
        "rejects compiled host/unit-test source",
        add_test_source,
        lambda result: any("forbidden compile source" in error for error in result.errors),
    )

    def add_test_framework(root: Path, build: Path) -> None:
        description_path = build / "project_description.json"
        description = json.loads(description_path.read_text(encoding="utf-8"))
        description["build_components"].append("unity")
        description["build_component_paths"].append(str(root.parent / "esp-idf-v5.4.4/components/unity"))
        write_json(description_path, description)
        with (build / "shahbaz_sensor_usb_node.map").open("a", encoding="utf-8") as stream:
            stream.write("LOAD " + str(build / "esp-idf/unity/libunity.a") + "\n")

    exercise(
        "rejects test framework component/archive",
        add_test_framework,
        lambda result: any("test/development component" in error for error in result.errors)
        and any("test framework archive" in error for error in result.errors),
    )

    def contaminate_disabled_actuator_graph(root: Path, build: Path) -> None:
        description_path = build / "project_description.json"
        description = json.loads(description_path.read_text(encoding="utf-8"))
        description["build_components"].extend(["actuator_espidf", "esp_driver_ledc"])
        description["build_component_paths"].extend([
            str(root / "components/actuator_espidf"),
            str(root.parent / "esp-idf-v5.4.4/components/esp_driver_ledc"),
        ])
        write_json(description_path, description)
        with (build / "shahbaz_sensor_usb_node.map").open("a", encoding="utf-8") as stream:
            stream.write("LOAD " + str(build / "esp-idf/actuator_espidf/libactuator_espidf.a") + "\n")
            stream.write("LOAD " + str(build / "esp-idf/esp_driver_ledc/libesp_driver_ledc.a") + "\n")

    exercise(
        "rejects physical actuator/LEDC graph when disabled",
        contaminate_disabled_actuator_graph,
        lambda result: any(
            "disabled-actuator production graph contains forbidden component" in error
            for error in result.errors
        ) and any(
            "disabled-actuator production archive reached application link" in error
            for error in result.errors
        ),
    )

    def remove_null_actuator(_root: Path, build: Path) -> None:
        description_path = build / "project_description.json"
        description = json.loads(description_path.read_text(encoding="utf-8"))
        index = description["build_components"].index("actuator_null")
        description["build_components"].pop(index)
        description["build_component_paths"].pop(index)
        write_json(description_path, description)

    exercise(
        "requires null actuator in disabled production graph",
        remove_null_actuator,
        lambda result: any(
            "disabled-actuator production graph is missing actuator_null" in error
            for error in result.errors
        ),
    )

    def add_stale_bt_config(_root: Path, build: Path) -> None:
        config = build.parent / "sdkconfig"
        with config.open("a", encoding="utf-8") as stream:
            stream.write("# CONFIG_BT_ENABLED is not set\n")

    exercise(
        "rejects stale unavailable Bluetooth config symbol",
        add_stale_bt_config,
        lambda result: any("must not contain unavailable CONFIG_BT_ENABLED" in error
                           for error in result.errors),
    )

    def add_linked_script(root: Path, build: Path) -> None:
        with (build / "shahbaz_sensor_usb_node.map").open("a", encoding="utf-8") as stream:
            stream.write("LOAD " + str(root / "tools/windows_hil_test.py") + "\n")

    exercise(
        "rejects linked development asset",
        add_linked_script,
        lambda result: any("linker map line" in error for error in result.errors),
    )

    def enable_actuators(root: Path, _build: Path) -> None:
        config = root / "sdkconfig"
        with config.open("a", encoding="utf-8") as stream:
            stream.write("CONFIG_SHAHBAZ_ACTUATORS_ENABLE=y\n")

    exercise(
        "rejects unsafe production config drift",
        enable_actuators,
        lambda result: any("enables physical actuators" in error for error in result.errors),
    )

    with tempfile.TemporaryDirectory(prefix="shahbaz-production-verifier-empty-") as temp:
        root = Path(temp)
        result = ProductionBuildVerifier(root, root / "build", require_build=False).verify()
        if result.skipped and not result.errors:
            print("[PASS] self-test: clean pre-build is a graceful SKIP")
        else:
            failures.append(f"pre-build skip: skipped={result.skipped}, errors={result.errors}")

    for failure in failures:
        print(f"[FAIL] self-test: {failure}", file=sys.stderr)
    if failures:
        print(f"Production build verifier self-test failed with {len(failures)} error(s).")
        return 1
    print("[PASS] production build verifier self-test")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project-root", type=Path, default=DEFAULT_ROOT)
    parser.add_argument("--build-dir", type=Path,
                        help="ESP-IDF build directory (default: <project-root>/build)")
    parser.add_argument("--require-build", action="store_true",
                        help="fail instead of SKIP when no target build is present")
    parser.add_argument("--self-test", action="store_true",
                        help="run synthetic contamination regression tests")
    args = parser.parse_args(argv)
    if args.self_test:
        return run_self_test()
    root = Path(os.path.abspath(args.project_root))
    build_dir = Path(os.path.abspath(args.build_dir or (root / "build")))
    result = ProductionBuildVerifier(root, build_dir, args.require_build).verify()
    print_result(result, build_dir)
    return 1 if result.errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
