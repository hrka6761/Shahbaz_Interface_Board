#!/usr/bin/env python3
"""Validate the repository-wide Persian/English Markdown documentation policy."""
from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
DOC_ROOT = ROOT / "doc"
DOC_FA = DOC_ROOT / "فارسی"
DOC_EN = DOC_ROOT / "English"
LINK_RE = re.compile(r"\[[^\]]*\]\(([^)]+)\)")
PERSIAN_RE = re.compile(r"[\u0600-\u06FF]")
ASCII_LETTER_RE = re.compile(r"[A-Za-z]")
# Invisible bidi controls are forbidden in Persian Markdown.  They can become visible
# in some editors/renderers and make mixed Persian/English prose unpredictable.
BIDI_CONTROL_RE = re.compile(r"[\u061C\u200E\u200F\u202A-\u202E\u2066-\u2069]")
EXPLICIT_LTR_RE = re.compile(r'<(?:code|span)\s+dir="ltr">.*?</(?:code|span)>')

# Generated artifacts and pinned third-party trees are outside Shahbaz's
# bilingual documentation contract. A target build or local tool install must
# not make repository-owned documentation validation fail.
EXCLUDED_MARKDOWN_ROOTS = {
    ".tooling",
    "artifacts",
    "build",
    "build-host",
    "managed_components",
}

# Persian technical documentation keeps widely used software/electronics terms
# in their standard English form.  These Persian translations/transliterations
# are intentionally forbidden because they reduce clarity for developers.
FORBIDDEN_TRANSLATED_TECH_TERMS = {
    "نصب": "Install",
    "پین": "Pin",
    "پورت": "Port",
    "میان‌افزار": "Firmware",
    "کامپوننت": "Component",
    "عملگر": "Actuator",
    "سروو": "Servo",
    "سنسور": "Sensor",
    "باس": "Bus",
    "تله‌متری": "Telemetry",
    "فریم": "Frame",
    "محموله": "Payload",
    "پیام ضربان": "Heartbeat",
    "مهلت زمانی": "Timeout",
    "زمان‌بند": "Scheduler",
    "پروتکل": "Protocol",
    "دیتاشیت": "Datasheet",
    "کامپایل": "Compile",
    "فلش": "Flash",
    "درایور": "Driver",
    "میزبان": "Host",
    "سکو": "Platform",
    "دامنه": "Domain",
    "آداپتور": "Adapter",
    "تراکنش": "Transaction",
    "پاورشل": "PowerShell",
    "پایشگر سریال": "Serial Monitor",
    "کلاینت": "Client",
    "پروگرام": "Programming",
    "مسلح‌سازی": "Arming",
    "مسلح‌شدن": "Arming",
    "جریان بایت": "Byte Stream",
    "نشست": "Session",
    "تخصیص حافظه": "memory allocation",
}


def validate_persian_direction(path: Path, text: str, errors: list[str]) -> None:
    """Enforce predictable Persian/LTR rendering without invisible Unicode controls.

    Rules for Persian Markdown:
    * no Unicode bidi-control character is allowed anywhere;
    * fenced code blocks are standalone technical/LTR contexts;
    * Markdown link destinations are non-visible and ignored;
    * visible Latin text in prose must be inside an explicit HTML element with
      ``dir="ltr"``.  This keeps direction intent visible in the source file.
    """
    for match in BIDI_CONTROL_RE.finditer(text):
        line_no = text.count("\n", 0, match.start()) + 1
        fail(
            f"Forbidden invisible bidi control in {path.relative_to(ROOT)}:{line_no} "
            f"(U+{ord(match.group(0)):04X})",
            errors,
        )

    for persian_term, preferred in FORBIDDEN_TRANSLATED_TECH_TERMS.items():
        start = 0
        while True:
            pos = text.find(persian_term, start)
            if pos < 0:
                break
            line_no = text.count("\n", 0, pos) + 1
            fail(
                f"Translated technical term in {path.relative_to(ROOT)}:{line_no}: "
                f"{persian_term!r}; use the standard English term {preferred!r} in explicit dir=ltr markup",
                errors,
            )
            start = pos + len(persian_term)

    in_fence = False
    for line_no, line in enumerate(text.splitlines(), 1):
        if line.lstrip().startswith("```"):
            in_fence = not in_fence
            continue
        if in_fence:
            continue

        # Link destinations are not rendered as prose.  Explicit LTR HTML elements
        # are the only accepted visible Latin context inside Persian documents.
        visible = re.sub(r"\[([^\]]*)\]\([^)]+\)", r"[\1]", line)
        visible = EXPLICIT_LTR_RE.sub("", visible)
        if ASCII_LETTER_RE.search(visible):
            fail(
                f"Visible Latin text without explicit dir=ltr in Persian Markdown: "
                f"{path.relative_to(ROOT)}:{line_no}",
                errors,
            )


def fail(message: str, errors: list[str]) -> None:
    errors.append(message)


def language_of(path: Path) -> str | None:
    if path.name.endswith(".fa.md"):
        return "fa"
    if path.name.endswith(".en.md"):
        return "en"
    return None


def base_name(path: Path, lang: str) -> str:
    return path.name[: -len(f".{lang}.md")]


def counterpart_for(path: Path, lang: str) -> Path:
    base = base_name(path, lang)
    other = "en" if lang == "fa" else "fa"

    # User-facing documents under doc are intentionally separated by language.
    if path.parent == DOC_FA:
        return DOC_EN / f"{base}.en.md"
    if path.parent == DOC_EN:
        return DOC_FA / f"{base}.fa.md"

    # Markdown outside doc keeps the two language variants side by side.
    return path.with_name(f"{base}.{other}.md")


def logical_key(path: Path, lang: str) -> tuple[Path, str]:
    base = base_name(path, lang)
    if path.parent in {DOC_FA, DOC_EN}:
        return (DOC_ROOT, base)
    return (path.parent, base)


def discover_markdown(root: Path) -> list[Path]:
    return sorted(
        path for path in root.rglob("*.md")
        if path.relative_to(root).parts[0].lower() not in EXCLUDED_MARKDOWN_ROOTS
    )


def discovery_self_test() -> int:
    with tempfile.TemporaryDirectory(prefix="shahbaz-bilingual-discovery-") as temp:
        root = Path(temp)
        expected = root / "README.en.md"
        expected.write_text("project document\n", encoding="utf-8")
        for excluded in sorted(EXCLUDED_MARKDOWN_ROOTS):
            candidate = root / excluded / "upstream.md"
            candidate.parent.mkdir(parents=True, exist_ok=True)
            candidate.write_text("third-party/generated document\n", encoding="utf-8")
        found = discover_markdown(root)
        if found != [expected]:
            print(f"[FAIL] bilingual Markdown discovery boundary: {found}")
            return 1
    print("[PASS] bilingual Markdown discovery excludes generated/toolchain/dependency roots")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true",
                        help="validate generated/third-party discovery exclusions")
    args = parser.parse_args(argv)
    if args.self_test:
        return discovery_self_test()

    errors: list[str] = []
    markdown = discover_markdown(ROOT)

    # The doc folder must contain only the two language directories.
    if not DOC_FA.is_dir():
        fail("Missing Persian documentation directory: doc/فارسی", errors)
    if not DOC_EN.is_dir():
        fail("Missing English documentation directory: doc/English", errors)

    if DOC_ROOT.is_dir():
        for entry in DOC_ROOT.iterdir():
            if entry not in {DOC_FA, DOC_EN}:
                fail(f"Unexpected entry directly under doc/: {entry.relative_to(ROOT)}", errors)

    for path in markdown:
        lang = language_of(path)
        if lang is None:
            fail(f"Markdown file has no language suffix: {path.relative_to(ROOT)}", errors)
            continue

        # Documents inside doc must be in the folder matching their language.
        if DOC_ROOT in path.parents:
            expected_parent = DOC_FA if lang == "fa" else DOC_EN
            if path.parent != expected_parent:
                fail(
                    f"Documentation file is in the wrong language directory: {path.relative_to(ROOT)}; "
                    f"expected under {expected_parent.relative_to(ROOT)}",
                    errors,
                )

        counterpart = counterpart_for(path, lang)
        if not counterpart.is_file():
            fail(
                f"Missing counterpart for {path.relative_to(ROOT)}: "
                f"expected {counterpart.relative_to(ROOT)}",
                errors,
            )

        text = path.read_text(encoding="utf-8")
        if lang == "fa":
            validate_persian_direction(path, text, errors)
        first_lines = "\n".join(text.splitlines()[:8])
        if counterpart.name not in first_lines:
            fail(
                f"Language-switch link missing near top of {path.relative_to(ROOT)} "
                f"(expected reference to {counterpart.name})",
                errors,
            )

        for match in LINK_RE.finditer(text):
            target = match.group(1).strip()
            if not target or target.startswith(("http://", "https://", "mailto:", "#")):
                continue
            target = target.split("#", 1)[0]
            resolved = (path.parent / target).resolve()
            try:
                resolved.relative_to(ROOT.resolve())
            except ValueError:
                fail(f"Local Markdown link escapes repository in {path.relative_to(ROOT)}: {target}", errors)
                continue
            if not resolved.exists():
                fail(f"Broken Markdown link in {path.relative_to(ROOT)}: {target}", errors)

    # Every logical document must have exactly one Persian and one English file.
    logical: dict[tuple[Path, str], set[str]] = {}
    for path in markdown:
        lang = language_of(path)
        if lang is None:
            continue
        logical.setdefault(logical_key(path, lang), set()).add(lang)

    for (parent, base), langs in sorted(logical.items(), key=lambda x: str(x[0])):
        if langs != {"fa", "en"}:
            fail(f"Incomplete bilingual pair: {(parent / base).relative_to(ROOT)} -> {sorted(langs)}", errors)

    if errors:
        for error in errors:
            print(f"[FAIL] {error}")
        print(f"Bilingual documentation validation failed with {len(errors)} error(s).")
        return 1

    print(
        f"[PASS] bilingual Markdown documentation: {len(markdown)} files, "
        f"{len(logical)} Persian/English pairs, doc/فارسی + doc/English layout, "
        "no broken local Markdown links, Persian direction/terminology policy valid (no hidden bidi controls; explicit dir=ltr; standard technical English terms preserved)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
