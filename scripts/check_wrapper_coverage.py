#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import json
import re
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parent.parent
HEADERS = [
    REPO_ROOT / "include" / "astl" / "astl_errors.h",
    REPO_ROOT / "include" / "astl" / "astl_version.h.in",
    REPO_ROOT / "include" / "astl" / "astl_telemetry.h",
]
PYTHON_CORE = REPO_ROOT / "python" / "astl" / "_core.pyx"
PYTHON_INIT = REPO_ROOT / "python" / "astl" / "__init__.py"
GO_WRAPPER = REPO_ROOT / "Go" / "astl" / "astl.go"
MAPPING_FILE = REPO_ROOT / "scripts" / "wrapper_coverage.json"


FUNCTION_PATTERN = re.compile(r"ASTL_API\s+[^;]*?\b(astl[A-Z][A-Za-z0-9_]+)\s*\(", re.DOTALL)
STATUS_PATTERN = re.compile(r"\bASTL_STATUS_[A-Z0-9_]+\b")
PYTHON_FUNCTION_PATTERN = r"\b(?:cpdef|def)\s+(?:[A-Za-z_][A-Za-z0-9_]*\s+)?{name}\s*\("
GO_FUNCTION_PATTERN = r"\bfunc\s+(?:\([^)]*\)\s*)?{name}\s*\("
PYTHON_STATUS_PATTERN = re.compile(r"^\s+([A-Z][A-Z0-9_]+)\s*=\s*ASTL_STATUS_[A-Z0-9_]+", re.MULTILINE)
GO_STATUS_PATTERN = re.compile(r"^\s*(Status[A-Za-z0-9]+)\s+Status\s*=", re.MULTILINE)


def load_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def snake_upper_to_camel(value: str) -> str:
    words: list[str] = []
    for part in value.split("_"):
        lowered = part.lower()
        if lowered == "api":
            words.append("API")
        else:
            words.append(lowered.capitalize())
    return "".join(words)


def extract_c_functions() -> list[str]:
    functions: list[str] = []
    for header in HEADERS:
        text = load_text(header)
        functions.extend(FUNCTION_PATTERN.findall(text))
    return sorted(set(functions))


def extract_c_statuses() -> list[str]:
    return sorted(set(STATUS_PATTERN.findall(load_text(REPO_ROOT / "include" / "astl" / "astl_errors.h"))))


def load_mapping() -> dict[str, dict[str, dict[str, str]]]:
    data = json.loads(load_text(MAPPING_FILE))
    return data["functions"]


def python_function_exists(symbol: str) -> bool:
    core_text = load_text(PYTHON_CORE)
    init_text = load_text(PYTHON_INIT)
    return bool(re.search(PYTHON_FUNCTION_PATTERN.format(name=re.escape(symbol)), core_text)) and symbol in init_text


def go_function_exists(symbol: str) -> bool:
    return bool(re.search(GO_FUNCTION_PATTERN.format(name=re.escape(symbol)), load_text(GO_WRAPPER)))


def python_status_constants() -> set[str]:
    return set(PYTHON_STATUS_PATTERN.findall(load_text(PYTHON_CORE)))


def go_status_constants() -> set[str]:
    return set(GO_STATUS_PATTERN.findall(load_text(GO_WRAPPER)))


def validate_wrapper_entry(
    c_function: str,
    wrapper_name: str,
    wrapper: dict[str, str] | None,
    exists_fn,
) -> tuple[list[str], list[str]]:
    errors: list[str] = []
    warnings: list[str] = []
    if wrapper is None:
        errors.append(f"{c_function}: missing {wrapper_name} coverage entry.")
        return errors, warnings

    mode = wrapper.get("mode")
    if mode == "wrapped":
        symbol = wrapper.get("symbol", "")
        if not symbol:
            errors.append(f"{c_function}: {wrapper_name} wrapped entry is missing a symbol.")
        elif not exists_fn(symbol):
            errors.append(f"{c_function}: expected {wrapper_name} wrapper symbol '{symbol}' was not found.")
    elif mode == "skipped":
        reason = wrapper.get("reason", "")
        if not reason:
            errors.append(f"{c_function}: {wrapper_name} skipped entry is missing a reason.")
        else:
            warnings.append(f"{c_function}: {wrapper_name} skipped: {reason}")
    else:
        errors.append(f"{c_function}: {wrapper_name} has unsupported mode '{mode}'.")

    return errors, warnings


def validate_function_mapping(c_functions: list[str], mapping: dict[str, dict[str, dict[str, str]]]) -> tuple[list[str], list[str]]:
    errors: list[str] = []
    warnings: list[str] = []
    mapped = set(mapping)
    discovered = set(c_functions)

    missing_in_mapping = sorted(discovered - mapped)
    stale_in_mapping = sorted(mapped - discovered)

    if missing_in_mapping:
        errors.append(
            "Public C API functions missing from scripts/wrapper_coverage.json: "
            + ", ".join(missing_in_mapping)
        )
    if stale_in_mapping:
        errors.append(
            "Stale wrapper coverage entries no longer present in public C headers: "
            + ", ".join(stale_in_mapping)
        )

    for c_function in sorted(discovered & mapped):
        for wrapper_name, exists_fn in (("python", python_function_exists), ("go", go_function_exists)):
            entry_errors, entry_warnings = validate_wrapper_entry(
                c_function,
                wrapper_name,
                mapping[c_function].get(wrapper_name),
                exists_fn,
            )
            errors.extend(entry_errors)
            warnings.extend(entry_warnings)
    return errors, warnings


def validate_status_coverage() -> list[str]:
    errors: list[str] = []
    c_statuses = extract_c_statuses()
    expected_python = {status.removeprefix("ASTL_STATUS_") for status in c_statuses}
    expected_go = {"Status" + snake_upper_to_camel(status.removeprefix("ASTL_STATUS_")) for status in c_statuses}

    python_constants = python_status_constants()
    go_constants = go_status_constants()

    missing_python = sorted(expected_python - python_constants)
    stale_python = sorted(python_constants - expected_python)
    missing_go = sorted(expected_go - go_constants)
    stale_go = sorted(go_constants - expected_go)

    if missing_python:
        errors.append("Python Status wrapper is missing C status codes: " + ", ".join(missing_python))
    if stale_python:
        errors.append("Python Status wrapper has stale status codes: " + ", ".join(stale_python))
    if missing_go:
        errors.append("Go Status wrapper is missing C status codes: " + ", ".join(missing_go))
    if stale_go:
        errors.append("Go Status wrapper has stale status codes: " + ", ".join(stale_go))

    return errors


def main() -> int:
    errors: list[str] = []
    warnings: list[str] = []
    c_functions = extract_c_functions()
    mapping = load_mapping()

    mapping_errors, mapping_warnings = validate_function_mapping(c_functions, mapping)
    errors.extend(mapping_errors)
    warnings.extend(mapping_warnings)
    errors.extend(validate_status_coverage())

    for warning in warnings:
        print(f"Wrapper coverage warning: {warning}")

    if errors:
        print("Wrapper coverage check failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1

    print("Wrapper coverage check passed.")
    print(f"  Public C functions covered or annotated: {len(c_functions)}")
    print(f"  Public C status codes mirrored in Python and Go: {len(extract_c_statuses())}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
