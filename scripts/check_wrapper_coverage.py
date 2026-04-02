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
ENUM_TYPEDEF_PATTERN = re.compile(r"typedef enum _(?P<tag>[A-Za-z0-9_]+)\s*{(?P<body>.*?)}\s*(?P<name>[A-Za-z0-9_]+)\s*;", re.DOTALL)
ENUM_ENTRY_PATTERN_TEMPLATE = r"\b({prefix}[A-Z0-9_]+)\b\s*=\s*([^,\n/]+)"
PYTHON_FUNCTION_PATTERN = r"\b(?:cpdef|def)\s+(?:[A-Za-z_][A-Za-z0-9_]*\s+)?{name}\s*\("
GO_FUNCTION_PATTERN = r"\bfunc\s+(?:\([^)]*\)\s*)?{name}\s*\("
PYTHON_STATUS_PATTERN = re.compile(r"^\s+([A-Z][A-Z0-9_]+)\s*=\s*ASTL_STATUS_[A-Z0-9_]+", re.MULTILINE)
GO_STATUS_PATTERN = re.compile(r"^\s*(Status[A-Za-z0-9]+)\s+Status\s*=", re.MULTILINE)
PYTHON_CLASS_PATTERN_TEMPLATE = r"^class {name}:\n(?P<body>(?:^(?:    |\t).*\n)+)"
PYTHON_CLASS_CONSTANT_PATTERN = re.compile(r"^\s+([A-Z][A-Z0-9_]*)\s*=\s*([A-Z0-9_]+)", re.MULTILINE)
GO_TYPED_CONSTANT_PATTERN = re.compile(r"^\s*(\w+)\s+(\w+)\s*=\s*(.+)$", re.MULTILINE)

METRIC_IDENTIFIER_SUFFIXES = (
    "UNKNOWN",
    "COUNT",
    "TEMPERATURE",
    "THERMAL_LIMIT",
    "THERMAL_THROTTLE",
    "ENERGY",
    "POWER",
    "POWER_LIMIT",
    "POWER_THROTTLE",
    "FREQUENCY",
    "VOLTAGE",
    "CURRENT",
    "BANDWIDTH",
    "FAN_SPEED",
    "HUMIDITY",
    "STATUS",
)

GO_VALUE_TYPE_CONSTANTS = {
    "ValueUnknown": "ASTL_VALUE_UNKNOWN",
    "ValueUInt8": "ASTL_VALUE_UINT8",
    "ValueUInt16": "ASTL_VALUE_UINT16",
    "ValueUInt32": "ASTL_VALUE_UINT32",
    "ValueUInt64": "ASTL_VALUE_UINT64",
    "ValueFloat32": "ASTL_VALUE_FLOAT32",
    "ValueFloat64": "ASTL_VALUE_FLOAT64",
    "ValueBool8": "ASTL_VALUE_BOOL8",
}

GO_COUNTER_TYPE_CONSTANTS = {
    "CounterTypeUnknown": "ASTL_COUNTER_TYPE_UNKNOWN",
    "CounterTypeValue": "ASTL_COUNTER_TYPE_VALUE",
    "CounterTypeCount": "ASTL_COUNTER_TYPE_COUNT",
    "CounterTypeEvent": "ASTL_COUNTER_TYPE_EVENT",
}

GO_METRIC_TYPE_CONSTANTS = {
    "MetricUnknown": "ASTL_METRIC_UNKNOWN",
    "MetricValue": "ASTL_METRIC_VALUE",
    "MetricFiniteSetValue": "ASTL_METRIC_FINITE_SET_VALUE",
    "MetricEvent": "ASTL_METRIC_EVENT",
    "MetricDelta": "ASTL_METRIC_DELTA",
    "MetricResidency": "ASTL_METRIC_RESIDENCY",
    "MetricRate": "ASTL_METRIC_RATE",
}

GO_COLLECTION_MODE_CONSTANTS = {
    "CollectionModeSampling": "ASTL_COLLECTION_MODE_SAMPLING",
    "CollectionModeImmediate": "ASTL_COLLECTION_MODE_IMMEDIATE",
    "CollectionModeSnapshot": "ASTL_COLLECTION_MODE_SNAPSHOT",
}

GO_COLLECTION_PARAMETER_FLAGS_CONSTANTS = {
    "CollectionParameterFlagNone": "ASTL_COLLECTION_PARAMETERS_FLAG_NONE",
    "CollectionParameterFlagOptimizeOverhead": "ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_OVERHEAD",
    "CollectionParameterFlagOptimizeMemory": "ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_MEMORY",
    "CollectionParameterFlagOptimizeInterference": "ASTL_COLLECTION_PARAMETERS_FLAG_OPTIMIZE_INTERFERENCE",
}

GO_METRIC_STATISTICS_FLAGS_CONSTANTS = {
    "MetricStatisticsFlagRegularAverage": "ASTL_METRIC_STATISTICS_FLAG_REGULAR_AVG",
    "MetricStatisticsFlagTimeWeightedAverage": "ASTL_METRIC_STATISTICS_FLAG_TIME_WEIGHTED_AVG",
}

GO_SYSTEM_INFO_FLAGS_CONSTANTS = {
    "SystemInfoFlagHost": "ASTL_SYSTEM_INFO_FLAG_HOST",
    "SystemInfoFlagLoadedSession": "ASTL_SYSTEM_INFO_FLAG_LOADED_SESSION",
}

GO_UNITS_CONSTANTS = {
    "UnitsUnknown": "ASTL_UNITS_UNKNOWN",
    "UnitsNone": "ASTL_UNITS_NONE",
    "UnitsTicks": "ASTL_UNITS_TICKS",
    "UnitsSeconds": "ASTL_UNITS_SECONDS",
    "UnitsCelsius": "ASTL_UNITS_CELSIUS",
    "UnitsJoules": "ASTL_UNITS_JOULES",
    "UnitsWatts": "ASTL_UNITS_WATTS",
    "UnitsVolts": "ASTL_UNITS_VOLTS",
    "UnitsAmps": "ASTL_UNITS_AMPS",
    "UnitsBytes": "ASTL_UNITS_BYTES",
    "UnitsMBytesPerSec": "ASTL_UNITS_MBYTESPERSEC",
    "UnitsMHz": "ASTL_UNITS_MHERTZ",
    "UnitsRPM": "ASTL_UNITS_RPM",
    "UnitsCount": "ASTL_UNITS_COUNT",
    "UnitsPercent": "ASTL_UNITS_PERCENT",
}


def go_enum_specs() -> list[tuple[str, dict[str, str], str, str, str]]:
    statuses = extract_c_statuses()
    return [
        (
            "Status",
            {f"Status{snake_upper_to_camel(status.removeprefix('ASTL_STATUS_'))}": status for status in statuses},
            "Status",
            "astl_status_code",
            "ASTL_STATUS_",
        ),
        ("ValueType", GO_VALUE_TYPE_CONSTANTS, "ValueType", "astl_value_type_t", "ASTL_VALUE_"),
        ("CounterType", GO_COUNTER_TYPE_CONSTANTS, "CounterType", "astl_counter_type_t", "ASTL_COUNTER_TYPE_"),
        ("MetricType", GO_METRIC_TYPE_CONSTANTS, "MetricType", "astl_metric_type_t", "ASTL_METRIC_"),
        (
            "MetricIdentifier",
            GO_METRIC_IDENTIFIER_CONSTANTS,
            "MetricIdentifier",
            "astl_metric_identifier_t",
            "ASTL_METRIC_IDENTIFIER_",
        ),
        ("Units", GO_UNITS_CONSTANTS, "Units", "astl_units_t", "ASTL_UNITS_"),
        (
            "CollectionMode",
            GO_COLLECTION_MODE_CONSTANTS,
            "CollectionMode",
            "astl_collection_mode_t",
            "ASTL_COLLECTION_MODE_",
        ),
        (
            "CollectionParameterFlags",
            GO_COLLECTION_PARAMETER_FLAGS_CONSTANTS,
            "CollectionParameterFlags",
            "astl_collection_parameters_flags_t",
            "ASTL_COLLECTION_PARAMETERS_FLAG_",
        ),
        (
            "MetricStatisticsFlags",
            GO_METRIC_STATISTICS_FLAGS_CONSTANTS,
            "MetricStatisticsFlags",
            "astl_metric_statistics_flags_t",
            "ASTL_METRIC_STATISTICS_FLAG_",
        ),
        (
            "SystemInfoFlags",
            GO_SYSTEM_INFO_FLAGS_CONSTANTS,
            "SystemInfoFlags",
            "astl_system_info_flags_t",
            "ASTL_SYSTEM_INFO_FLAG_",
        ),
    ]

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


def build_constant_mapping(
    suffixes: tuple[str, ...],
    c_prefix: str,
    name_prefix: str = "",
    transform=snake_upper_to_camel,
) -> dict[str, str]:
    return {
        f"{name_prefix}{transform(suffix)}" if name_prefix else suffix: f"{c_prefix}{suffix}"
        for suffix in suffixes
    }


def append_message(errors: list[str], prefix: str, items: list[str]) -> None:
    if items:
        errors.append(prefix + ", ".join(items))


def collect_missing_and_stale(expected: set[str], actual: set[str]) -> tuple[list[str], list[str]]:
    return sorted(expected - actual), sorted(actual - expected)


def validate_symbol_sets(
    expected: set[str],
    actual: set[str],
    missing_message: str,
    stale_message: str,
) -> list[str]:
    errors: list[str] = []
    missing, stale = collect_missing_and_stale(expected, actual)
    append_message(errors, missing_message, missing)
    append_message(errors, stale_message, stale)
    return errors


def collect_mismatched_constants(
    actual_constants: dict[str, str],
    expected_constants: dict[str, str],
    matches_expected,
) -> list[str]:
    return sorted(
        name
        for name, expected_symbol in expected_constants.items()
        if actual_constants.get(name) is not None and not matches_expected(name, expected_symbol)
    )


GO_METRIC_IDENTIFIER_CONSTANTS = build_constant_mapping(
    METRIC_IDENTIFIER_SUFFIXES,
    "ASTL_METRIC_IDENTIFIER_",
    name_prefix="MetricIdentifier",
)

PYTHON_METRIC_IDENTIFIER_CONSTANTS = build_constant_mapping(
    METRIC_IDENTIFIER_SUFFIXES,
    "ASTL_METRIC_IDENTIFIER_",
    transform=lambda value: value,
)


def extract_c_functions() -> list[str]:
    functions: list[str] = []
    for header in HEADERS:
        text = load_text(header)
        functions.extend(FUNCTION_PATTERN.findall(text))
    return sorted(set(functions))


def extract_c_statuses() -> list[str]:
    return sorted(set(STATUS_PATTERN.findall(load_text(REPO_ROOT / "include" / "astl" / "astl_errors.h"))))


def extract_c_enum_constants(enum_name: str, prefix: str) -> dict[str, str]:
    for header in HEADERS:
        text = load_text(header)
        for match in ENUM_TYPEDEF_PATTERN.finditer(text):
            if match.group("name") != enum_name:
                continue
            entry_pattern = re.compile(ENUM_ENTRY_PATTERN_TEMPLATE.format(prefix=re.escape(prefix)))
            return {name: value.strip() for name, value in entry_pattern.findall(match.group("body"))}
    # Do not raise here so that callers can report a structured wrapper-coverage  
    # failure instead of terminating with a traceback in CI.  
    sys.stderr.write(f"error: enum typedef '{enum_name}' not found in public headers\n")  
    return {}  


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


def python_class_constants(class_name: str) -> dict[str, str]:
    match = re.search(PYTHON_CLASS_PATTERN_TEMPLATE.format(name=re.escape(class_name)), load_text(PYTHON_CORE), re.MULTILINE)
    if match is None:
        return {}
    return {name: value.strip() for name, value in PYTHON_CLASS_CONSTANT_PATTERN.findall(match.group("body"))}


def go_typed_constants(type_name: str) -> dict[str, str]:
    constants: dict[str, str] = {}
    for name, declared_type, value in GO_TYPED_CONSTANT_PATTERN.findall(load_text(GO_WRAPPER)):
        if declared_type == type_name:
            constants[name] = value.strip()
    return constants


def normalize_go_constant_value(value: str) -> str:
    normalized = value.split("//", 1)[0].strip()
    while True:
        cast_match = re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*\((.+)\)", normalized)
        if cast_match is None:
            break
        normalized = cast_match.group(1).strip()
    return normalized


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


def validate_constant_mapping_inventory(
    c_constants: set[str],
    expected_symbols: set[str],
    missing_message: str,
    stale_message: str,
) -> list[str]:
    return validate_symbol_sets(c_constants, expected_symbols, missing_message, stale_message)


def validate_wrapper_constants(
    actual_constants: dict[str, str],
    expected_constants: dict[str, str],
    missing_message: str,
    stale_message: str,
    mismatched_message: str,
) -> list[str]:
    errors: list[str] = []
    missing, stale = collect_missing_and_stale(set(expected_constants), set(actual_constants))
    mismatched = collect_mismatched_constants(
        actual_constants,
        expected_constants,
        lambda name, expected_symbol: actual_constants[name] == expected_symbol,
    )
    append_message(errors, missing_message, missing)
    append_message(errors, stale_message, stale)
    append_message(errors, mismatched_message, [f"{name}={actual_constants[name]}" for name in mismatched])
    return errors


def validate_status_coverage() -> list[str]:
    c_statuses = extract_c_statuses()
    expected_python = {status.removeprefix("ASTL_STATUS_") for status in c_statuses}
    expected_go = {"Status" + snake_upper_to_camel(status.removeprefix("ASTL_STATUS_")) for status in c_statuses}
    return validate_symbol_sets(
        expected_python,
        python_status_constants(),
        "Python Status wrapper is missing C status codes: ",
        "Python Status wrapper has stale status codes: ",
    ) + validate_symbol_sets(
        expected_go,
        go_status_constants(),
        "Go Status wrapper is missing C status codes: ",
        "Go Status wrapper has stale status codes: ",
    )


def validate_python_metric_identifier_coverage() -> list[str]:
    c_identifier_constants = set(extract_c_enum_constants("astl_metric_identifier_t", "ASTL_METRIC_IDENTIFIER_"))
    python_constants = python_class_constants("MetricIdentifier")
    return validate_constant_mapping_inventory(
        c_identifier_constants,
        set(PYTHON_METRIC_IDENTIFIER_CONSTANTS.values()),
        "Wrapper coverage script is missing Python MetricIdentifier mappings for C identifiers: ",
        "Wrapper coverage script has stale Python MetricIdentifier mappings for removed C identifiers: ",
    ) + validate_wrapper_constants(
        python_constants,
        PYTHON_METRIC_IDENTIFIER_CONSTANTS,
        "Python MetricIdentifier wrapper is missing C identifier codes: ",
        "Python MetricIdentifier wrapper has stale identifier codes: ",
        "Python MetricIdentifier wrapper has mismatched identifier mappings: ",
    )


def go_constant_matches_c_symbol(
    value: str,
    type_name: str,
    c_symbol: str,
    c_enum_constants: dict[str, str],
) -> bool:
    normalized = normalize_go_constant_value(value)
    if normalized == f"C.{c_symbol}":
        return True

    c_value = c_enum_constants.get(c_symbol, "").strip()
    if c_value == "-1" and normalized == f"^{type_name}(0)":
        return True

    return False


def validate_go_enum_coverage(
    type_name: str,
    expected_constants: dict[str, str],
    label: str,
    c_enum_name: str,
    c_prefix: str,
) -> list[str]:
    c_enum_constants = extract_c_enum_constants(c_enum_name, c_prefix)
    go_constants = go_typed_constants(type_name)
    errors = validate_constant_mapping_inventory(
        set(c_enum_constants),
        set(expected_constants.values()),
        f"Wrapper coverage script is missing Go {label} mappings for C {label.lower()} codes: ",
        f"Wrapper coverage script has stale Go {label} mappings for removed C {label.lower()} codes: ",
    )
    missing, stale = collect_missing_and_stale(set(expected_constants), set(go_constants))
    mismatched = collect_mismatched_constants(
        go_constants,
        expected_constants,
        lambda name, expected_symbol: go_constant_matches_c_symbol(
            go_constants[name], type_name, expected_symbol, c_enum_constants
        ),
    )
    append_message(errors, f"Go {label} wrapper is missing C {label.lower()} codes: ", missing)
    append_message(errors, f"Go {label} wrapper has stale {label.lower()} codes: ", stale)
    append_message(
        errors,
        f"Go {label} wrapper has mismatched {label.lower()} mappings: ",
        [f"{name}={go_constants[name]}" for name in mismatched],
    )
    return errors


def validate_go_enums() -> list[str]:
    errors: list[str] = []
    for type_name, expected_constants, label, c_enum_name, c_prefix in go_enum_specs():
        errors.extend(validate_go_enum_coverage(type_name, expected_constants, label, c_enum_name, c_prefix))
    return errors


def print_success_summary(c_functions: list[str]) -> None:
    summary = [
        ("Public C functions covered or annotated", len(c_functions)),
        ("Public C status codes mirrored in Python and Go", len(extract_c_statuses())),
        ("Python MetricIdentifier constants mirrored", len(PYTHON_METRIC_IDENTIFIER_CONSTANTS)),
        ("Go Status constants mirrored", len(extract_c_statuses())),
        ("Go ValueType constants mirrored", len(GO_VALUE_TYPE_CONSTANTS)),
        ("Go CounterType constants mirrored", len(GO_COUNTER_TYPE_CONSTANTS)),
        ("Go MetricType constants mirrored", len(GO_METRIC_TYPE_CONSTANTS)),
        ("Go MetricIdentifier constants mirrored", len(GO_METRIC_IDENTIFIER_CONSTANTS)),
        ("Go Units constants mirrored", len(GO_UNITS_CONSTANTS)),
        ("Go CollectionMode constants mirrored", len(GO_COLLECTION_MODE_CONSTANTS)),
        ("Go CollectionParameterFlags constants mirrored", len(GO_COLLECTION_PARAMETER_FLAGS_CONSTANTS)),
        ("Go MetricStatisticsFlags constants mirrored", len(GO_METRIC_STATISTICS_FLAGS_CONSTANTS)),
        ("Go SystemInfoFlags constants mirrored", len(GO_SYSTEM_INFO_FLAGS_CONSTANTS)),
    ]
    print("Wrapper coverage check passed.")
    for label, count in summary:
        print(f"  {label}: {count}")


def main() -> int:
    errors: list[str] = []
    warnings: list[str] = []
    c_functions = extract_c_functions()
    mapping = load_mapping()

    mapping_errors, mapping_warnings = validate_function_mapping(c_functions, mapping)
    errors.extend(mapping_errors)
    warnings.extend(mapping_warnings)
    for validator in (validate_status_coverage, validate_python_metric_identifier_coverage, validate_go_enums):
        errors.extend(validator())

    for warning in warnings:
        print(f"Wrapper coverage warning: {warning}")

    if errors:
        print("Wrapper coverage check failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1

    print_success_summary(c_functions)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
