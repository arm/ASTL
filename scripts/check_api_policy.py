#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

"""Enforce ASTL public API versioning and release-policy requirements."""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from enum import IntEnum
from pathlib import Path
from typing import Any, Iterable


REPO_ROOT = Path(__file__).resolve().parent.parent
PUBLIC_HEADER_NAMES = (
    "astl.h",
    "astl_errors.h",
    "astl_telemetry.h",
    "astl_utils.h",
)
EXCLUDED_MACROS = {
    "ASTL_VERSION",
    "ASTL_VERSION_STRING",
    "ASTL_VERSION_MAJOR",
    "ASTL_VERSION_MINOR",
    "ASTL_VERSION_MICRO",
}
INCLUDE_GUARDS = {
    "ASTL_UTILS_H_",
    "INCLUDE_ASTL_ERRORS_H_",
    "INCLUDE_ASTL_H_",
    "INCLUDE_ASTL_TELEMETRY_H_",
    "INCLUDE_ASTL_VERSION_H_",
}
TERMINAL_ENUMERATORS = {
    "_astl_status_code": "ASTL_STATUS_INTERNAL_ERROR",
}
STABLE_RELEASE_TAG = re.compile(r"^releases?/(\d+\.\d+\.\d+)$")

PR_DECLARATIONS = {
    "No public API or user-flow change": 0,
    "Backward-compatible public API addition": 1,
    "Breaking public API or user-flow change": 2,
}


class Severity(IntEnum):
    NONE = 0
    MINOR = 1
    MAJOR = 2

    @property
    def label(self) -> str:
        return self.name


@dataclass(frozen=True)
class Change:
    severity: Severity
    kind: str
    name: str
    detail: str


@dataclass
class ApiSnapshot:
    functions: dict[str, tuple[str, tuple[str, ...]]] = field(default_factory=dict)
    function_abi: dict[str, tuple[str, bool]] = field(default_factory=dict)
    records: dict[str, tuple[str, tuple[tuple[str, str, bool, str], ...]]] = field(default_factory=dict)
    enums: dict[str, tuple[tuple[str, str], ...]] = field(default_factory=dict)
    typedefs: dict[str, str] = field(default_factory=dict)
    variables: dict[str, str] = field(default_factory=dict)
    macros: dict[str, str] = field(default_factory=dict)


@dataclass(frozen=True)
class Version:
    major: int
    minor: int
    patch: int
    original: str

    @classmethod
    def parse(cls, text: str) -> Version:
        original = next(
            (line.strip() for line in text.splitlines() if line.strip() and not line.lstrip().startswith("#")),
            "",
        )
        normalized = original.removesuffix(".post")
        match = re.fullmatch(r"(\d+)\.(\d+)\.(\d+)", normalized)
        if match is None:
            raise ValueError(f"invalid ASTL version {original!r}; expected MAJOR.MINOR.PATCH with optional .post")
        return cls(*(int(part) for part in match.groups()), original=original)


def run(command: list[str], *, cwd: Path = REPO_ROOT, input_text: str | None = None) -> str:
    result = subprocess.run(
        command,
        cwd=cwd,
        input=input_text,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if result.returncode != 0:
        rendered = " ".join(command)
        raise RuntimeError(f"command failed ({result.returncode}): {rendered}\n{result.stderr.strip()}")
    return result.stdout


def git_text(ref: str, path: str) -> str:
    return run(["git", "show", f"{ref}:{path}"])


def read_version(ref: str | None) -> Version:
    text = git_text(ref, "VERSION.md") if ref else (REPO_ROOT / "VERSION.md").read_text(encoding="utf-8")
    return Version.parse(text)


def render_version_header(template: str, version: Version) -> str:
    return (
        template.replace("@ASTL_VERSION@", version.original.removesuffix(".post"))
        .replace("@PROJECT_VERSION_MAJOR@", str(version.major))
        .replace("@PROJECT_VERSION_MINOR@", str(version.minor))
        .replace("@PROJECT_VERSION_PATCH@", str(version.patch))
    )


def materialize_headers(destination: Path, ref: str | None) -> None:
    include_dir = destination / "astl"
    include_dir.mkdir(parents=True)
    for name in PUBLIC_HEADER_NAMES:
        relative = f"include/astl/{name}"
        text = git_text(ref, relative) if ref else (REPO_ROOT / relative).read_text(encoding="utf-8")
        (include_dir / name).write_text(text, encoding="utf-8")

    template_path = "include/astl/astl_version.h.in"
    template = git_text(ref, template_path) if ref else (REPO_ROOT / template_path).read_text(encoding="utf-8")
    (include_dir / "astl_version.h").write_text(render_version_header(template, read_version(ref)), encoding="utf-8")


def find_clang() -> str:
    configured = os.environ.get("CLANG")
    if configured:
        return configured
    for candidate in ("clang-20", "clang"):
        found = shutil.which(candidate)
        if found:
            return found
    raise RuntimeError("clang was not found; install Clang 20 or set CLANG")


def nested_value(node: dict[str, Any]) -> str:
    if "value" in node:
        return str(node["value"])
    for child in node.get("inner", []):
        value = nested_value(child)
        if value:
            return value
    return ""


def parse_function_node(snapshot: ApiSnapshot, node: dict[str, Any]) -> None:
    name = node["name"]
    if not name.startswith("astl"):
        return
    parameter_types = tuple(
        child.get("type", {}).get("qualType", "")
        for child in node.get("inner", [])
        if child.get("kind") == "ParmVarDecl"
    )
    snapshot.functions[name] = (node.get("type", {}).get("qualType", ""), parameter_types)


def parse_record_field(node: dict[str, Any]) -> tuple[str, str, bool, str]:
    is_bitfield = bool(node.get("isBitfield"))
    return (
        node.get("name", ""),
        node.get("type", {}).get("qualType", ""),
        is_bitfield,
        nested_value(node) if is_bitfield else "",
    )


def parse_record_node(snapshot: ApiSnapshot, node: dict[str, Any]) -> None:
    if not node.get("completeDefinition"):
        return
    fields = tuple(
        parse_record_field(child)
        for child in node.get("inner", [])
        if child.get("kind") == "FieldDecl"
    )
    snapshot.records[node["name"]] = (node.get("tagUsed", "struct"), fields)


def parse_enum_node(snapshot: ApiSnapshot, node: dict[str, Any]) -> None:
    constants = tuple(
        (child.get("name", ""), nested_value(child))
        for child in node.get("inner", [])
        if child.get("kind") == "EnumConstantDecl"
    )
    snapshot.enums[node["name"]] = constants


def parse_typedef_node(snapshot: ApiSnapshot, node: dict[str, Any]) -> None:
    snapshot.typedefs[node["name"]] = node.get("type", {}).get("qualType", "")


def parse_variable_node(snapshot: ApiSnapshot, node: dict[str, Any]) -> None:
    snapshot.variables[node["name"]] = node.get("type", {}).get("qualType", "")


AST_NODE_PARSERS = {
    "FunctionDecl": parse_function_node,
    "RecordDecl": parse_record_node,
    "EnumDecl": parse_enum_node,
    "TypedefDecl": parse_typedef_node,
    "VarDecl": parse_variable_node,
}


def is_astl_declaration(node: dict[str, Any]) -> bool:
    name = node.get("name", "")
    return bool(name) and (name.startswith("astl") or name.startswith("_astl"))


def parse_ast(root: dict[str, Any]) -> ApiSnapshot:
    snapshot = ApiSnapshot()
    for node in root.get("inner", []):
        if not is_astl_declaration(node):
            continue
        parser = AST_NODE_PARSERS.get(node.get("kind", ""))
        if parser is not None:
            parser(snapshot, node)
    return snapshot


def extract_function_abi(root: dict[str, Any]) -> dict[str, tuple[str, bool]]:
    """Fingerprint C linkage and explicit export visibility from a C++ AST."""
    functions: dict[str, tuple[str, bool]] = {}

    def visit(node: dict[str, Any], language_linkage: str = "C++") -> None:
        if node.get("kind") == "LinkageSpecDecl":
            language_linkage = node.get("language", language_linkage)

        name = node.get("name", "")
        if node.get("kind") == "FunctionDecl" and name.startswith("astl"):
            is_exported = any(
                (child.get("kind") == "VisibilityAttr" and child.get("visibility") == "default")
                or child.get("kind") in {"DLLExportAttr", "DLLImportAttr"}
                for child in node.get("inner", [])
            )
            functions[name] = (language_linkage, is_exported)

        for child in node.get("inner", []):
            visit(child, language_linkage)

    visit(root)
    return functions


def normalize_macro(value: str) -> str:
    return " ".join(value.strip().split())


def extract_macros(clang: str, include_root: Path) -> dict[str, str]:
    macros: dict[str, str] = {}
    pattern = re.compile(r"^#define\s+(ASTL_[A-Za-z0-9_]+(?:\([^)]*\))?)\s*(.*)$")
    profiles = (
        ("c17", ("-x", "c", "-std=c17")),
        ("c23", ("-x", "c", "-std=c23")),
        ("cxx20", ("-x", "c++", "-std=c++20")),
    )
    for profile, language_args in profiles:
        output = run(
            [
                clang,
                *language_args,
                f"-I{include_root}",
                "-include",
                "astl/astl.h",
                "-dM",
                "-E",
                "-",
            ],
            input_text="",
        )
        for line in output.splitlines():
            match = pattern.match(line)
            if match is None:
                continue
            name_with_args, value = match.groups()
            name = name_with_args.split("(", 1)[0]
            if name in EXCLUDED_MACROS or name in INCLUDE_GUARDS:
                continue
            macros[f"{name_with_args}@{profile}"] = normalize_macro(value)
    return macros


def build_snapshot(ref: str | None = None) -> ApiSnapshot:
    clang = find_clang()
    with tempfile.TemporaryDirectory(prefix="astl-api-") as temporary:
        root = Path(temporary)
        materialize_headers(root / "include", ref)
        probe = root / "api_probe.c"
        probe.write_text('#include "astl/astl.h"\n', encoding="utf-8")
        ast_text = run(
            [
                clang,
                "-x",
                "c",
                "-std=c17",
                f"-I{root / 'include'}",
                "-Xclang",
                "-ast-dump=json",
                "-fsyntax-only",
                str(probe),
            ]
        )
        snapshot = parse_ast(json.loads(ast_text))
        cpp_probe = root / "api_probe.cpp"
        cpp_probe.write_text('#include "astl/astl.h"\n', encoding="utf-8")
        cpp_ast_text = run(
            [
                clang,
                "-x",
                "c++",
                "-std=c++20",
                f"-I{root / 'include'}",
                "-Xclang",
                "-ast-dump=json",
                "-fsyntax-only",
                str(cpp_probe),
            ]
        )
        snapshot.function_abi = extract_function_abi(json.loads(cpp_ast_text))
        snapshot.macros = extract_macros(clang, root / "include")
        return snapshot


def compare_simple(
    changes: list[Change], kind: str, old: dict[str, Any], new: dict[str, Any], *, additions: Severity = Severity.MINOR
) -> None:
    for name in sorted(old.keys() - new.keys()):
        changes.append(Change(Severity.MAJOR, kind, name, "removed"))
    for name in sorted(new.keys() - old.keys()):
        changes.append(Change(additions, kind, name, "added"))
    for name in sorted(old.keys() & new.keys()):
        if old[name] != new[name]:
            changes.append(Change(Severity.MAJOR, kind, name, "changed"))


def compare_function_abi(old: ApiSnapshot, new: ApiSnapshot) -> list[Change]:
    changes: list[Change] = []
    for name in sorted(old.functions.keys() & new.functions.keys()):
        old_abi = old.function_abi.get(name)
        new_abi = new.function_abi.get(name)
        if old_abi != new_abi:
            changes.append(
                Change(
                    Severity.MAJOR,
                    "function ABI",
                    name,
                    f"linkage/export changed from {old_abi!r} to {new_abi!r}",
                )
            )
    return changes


def enum_extensible_values(
    name: str, values: tuple[tuple[str, str], ...]
) -> tuple[tuple[tuple[str, str], ...], tuple[str, str] | None]:
    terminal_name = TERMINAL_ENUMERATORS.get(name)
    if terminal_name is None:
        return values, None
    terminal = next((item for item in values if item[0] == terminal_name), None)
    return tuple(item for item in values if item[0] != terminal_name), terminal


def classify_record_change(
    name: str,
    old_record: tuple[str, tuple[tuple[str, str, bool, str], ...]],
    new_record: tuple[str, tuple[tuple[str, str, bool, str], ...]],
) -> Change | None:
    old_tag, old_fields = old_record
    new_tag, new_fields = new_record
    if old_record == new_record:
        return None

    is_tail_extension = (
        old_tag == new_tag == "struct"
        and len(new_fields) > len(old_fields)
        and new_fields[: len(old_fields)] == old_fields
    )
    if is_tail_extension:
        added = ", ".join(field[0] for field in new_fields[len(old_fields) :])
        # many but not all structs in the API start with a 'size' field to help version them.
        # adding members to the end of such structs is a MINOR change, but extending a struct
        # without a 'size' member, such as astl_sample_t is a  MAJOR change because we can't
        # support backwards API compatibility. 
        if old_fields and old_fields[0][0] == "size":
            return Change(Severity.MINOR, "struct", name, f"appended field(s): {added}")
        return Change(
            Severity.MAJOR,
            "struct",
            name,
            f"appended field(s) to struct without leading size: {added}",
        )
    return Change(Severity.MAJOR, old_tag or "record", name, "layout or existing field changed")


def compare_records(
    old: dict[str, tuple[str, tuple[tuple[str, str, bool, str], ...]]],
    new: dict[str, tuple[str, tuple[tuple[str, str, bool, str], ...]]],
) -> list[Change]:
    changes = [Change(Severity.MAJOR, "record", name, "removed") for name in sorted(old.keys() - new.keys())]
    changes.extend(
        Change(Severity.MINOR, new[name][0], name, "added") for name in sorted(new.keys() - old.keys())
    )
    for name in sorted(old.keys() & new.keys()):
        change = classify_record_change(name, old[name], new[name])
        if change is not None:
            changes.append(change)
    return changes


def classify_enum_change(
    name: str, old_enum: tuple[tuple[str, str], ...], new_enum: tuple[tuple[str, str], ...]
) -> Change | None:
    old_values, old_terminal = enum_extensible_values(name, old_enum)
    new_values, new_terminal = enum_extensible_values(name, new_enum)
    if old_values == new_values and old_terminal == new_terminal:
        return None

    is_append = (
        old_terminal == new_terminal
        and len(new_values) > len(old_values)
        and new_values[: len(old_values)] == old_values
    )
    if is_append:
        added = ", ".join(item[0] for item in new_values[len(old_values) :])
        return Change(Severity.MINOR, "enum", name, f"appended enumerator(s): {added}")
    return Change(Severity.MAJOR, "enum", name, "existing order, name, or value changed")


def compare_enums(
    old: dict[str, tuple[tuple[str, str], ...]], new: dict[str, tuple[tuple[str, str], ...]]
) -> list[Change]:
    changes = [Change(Severity.MAJOR, "enum", name, "removed") for name in sorted(old.keys() - new.keys())]
    changes.extend(Change(Severity.MINOR, "enum", name, "added") for name in sorted(new.keys() - old.keys()))
    for name in sorted(old.keys() & new.keys()):
        change = classify_enum_change(name, old[name], new[name])
        if change is not None:
            changes.append(change)
    return changes


def compare_snapshots(old: ApiSnapshot, new: ApiSnapshot) -> list[Change]:
    changes: list[Change] = []
    compare_simple(changes, "function", old.functions, new.functions)
    changes.extend(compare_function_abi(old, new))
    compare_simple(changes, "typedef", old.typedefs, new.typedefs)
    compare_simple(changes, "variable", old.variables, new.variables)
    compare_simple(changes, "macro", old.macros, new.macros)
    changes.extend(compare_records(old.records, new.records))
    changes.extend(compare_enums(old.enums, new.enums))
    return sorted(changes, key=lambda change: (-change.severity, change.kind, change.name))


def required_severity(changes: Iterable[Change]) -> Severity:
    return max((change.severity for change in changes), default=Severity.NONE)


def version_satisfies(base: Version, head: Version, required: Severity) -> bool:
    if required == Severity.NONE:
        return True
    if required == Severity.MAJOR:
        return head.major > base.major
    return head.major > base.major or (head.major == base.major and head.minor > base.minor)


def validate_candidate_version(
    release_ref: str,
    release_version: Version,
    candidate_version: Version | None,
    required: Severity,
) -> list[str]:
    if candidate_version is None or version_satisfies(release_version, candidate_version, required):
        return []
    component = "major" if required == Severity.MAJOR else "minor or major"
    return [
        f"{required.label} change requires a higher {component} version than "
        f"release {release_ref} ({release_version.original})"
    ]


def merge_base(base_ref: str) -> str:
    return run(["git", "merge-base", base_ref, "HEAD"]).strip()


def version_number(version: Version) -> tuple[int, int, int]:
    return version.major, version.minor, version.patch


def latest_release_ref(head_version: Version, exclude_version: Version | None = None) -> str:
    candidates: list[tuple[Version, str]] = []
    for tag in run(["git", "tag", "--list"]).splitlines():
        match = STABLE_RELEASE_TAG.fullmatch(tag)
        if match is not None:
            version = Version.parse(match.group(1))
            if (
                version_number(version) <= version_number(head_version)
                and (
                    exclude_version is None
                    or version_number(version) != version_number(exclude_version)
                )
            ):
                candidates.append((version, tag))
    if not candidates:
        raise RuntimeError(
            f"no stable release tag at or below {head_version.original} was found; pass --release-ref explicitly"
        )
    return max(candidates, key=lambda candidate: version_number(candidate[0]))[1]


def changed_files(base_ref: str) -> list[str]:
    return [line for line in run(["git", "diff", "--name-only", base_ref, "--"]).splitlines() if line]


def parse_pr_declaration(event_path: Path | None) -> tuple[Severity | None, list[str]]:
    if event_path is None or not event_path.is_file():
        return None, []
    event = json.loads(event_path.read_text(encoding="utf-8"))
    pull_request = event.get("pull_request")
    if not pull_request:
        return None, []
    body = pull_request.get("body") or ""
    selected = [
        label
        for label in PR_DECLARATIONS
        if re.search(rf"^-\s*\[[xX]\]\s*{re.escape(label)}\s*$", body, re.MULTILINE)
    ]
    if len(selected) != 1:
        return None, [
            "PR body must select exactly one public API declaration; selected: "
            + (", ".join(selected) if selected else "none")
        ]
    return Severity(PR_DECLARATIONS[selected[0]]), []


def added_changelog_entries(base_ref: str, heading: str) -> list[str]:
    path = REPO_ROOT / "CHANGELOG.md"
    if not path.is_file():
        return []
    current_lines = path.read_text(encoding="utf-8").splitlines()
    diff = run(["git", "diff", "--unified=0", base_ref, "--", "CHANGELOG.md"])
    added_numbers: list[int] = []
    new_line = 0
    for line in diff.splitlines():
        match = re.match(r"@@ -\d+(?:,\d+)? \+(\d+)(?:,\d+)? @@", line)
        if match:
            new_line = int(match.group(1))
        elif line.startswith("+") and not line.startswith("+++"):
            added_numbers.append(new_line)
            new_line += 1
        elif not line.startswith("-"):
            new_line += 1

    entries: list[str] = []
    current_section = ""
    inside_unreleased = False
    for number, line in enumerate(current_lines, start=1):
        if line.startswith("## "):
            inside_unreleased = line.strip() == "## Unreleased"
            current_section = ""
        elif inside_unreleased and line.startswith("### "):
            current_section = line.removeprefix("### ").strip()
        elif (
            number in added_numbers
            and inside_unreleased
            and current_section == heading
            and re.match(r"^\s*-\s+\S", line)
        ):
            entries.append(line.strip())
    return entries


def changelog_section_severity(text: str, section: str) -> Severity:
    severity = Severity.NONE
    current_section = ""
    inside_requested_section = False
    for line in text.splitlines():
        if line.startswith("## "):
            heading = line.removeprefix("## ").strip()
            inside_requested_section = heading == section or heading.startswith(f"{section} ")
            current_section = ""
        elif inside_requested_section and line.startswith("### "):
            current_section = line.removeprefix("### ").strip()
        elif inside_requested_section and re.match(r"^\s*-\s+\S", line):
            if current_section == "Breaking":
                severity = max(severity, Severity.MAJOR)
            elif current_section == "Added":
                severity = max(severity, Severity.MINOR)
    return severity


def unreleased_changelog_severity(text: str) -> Severity:
    return changelog_section_severity(text, "Unreleased")


def validate_repository_policy(
    base_ref: str,
    ast_severity: Severity,
    declared_severity: Severity | None,
    files: list[str],
) -> list[str]:
    errors: list[str] = []
    effective = max(ast_severity, declared_severity or Severity.NONE)
    if declared_severity is not None and declared_severity < ast_severity:
        errors.append(f"PR declares {declared_severity.label}, but public headers require {ast_severity.label}")
    if effective == Severity.NONE:
        return errors

    changelog_heading = "Breaking" if effective == Severity.MAJOR else "Added"
    if "CHANGELOG.md" not in files or not added_changelog_entries(base_ref, changelog_heading):
        errors.append(f"add an entry under CHANGELOG.md > Unreleased > {changelog_heading}")

    python_changed = any(
        (path.startswith("python/astl/") and not path.endswith("/_core.cpp")) or path.startswith("python/tests/")
        for path in files
    )
    go_changed = any(path.startswith("Go/astl/") and path.endswith(".go") for path in files)
    if not python_changed:
        errors.append("public API changes must be reflected in non-generated Python wrapper code or tests")
    if not go_changed:
        errors.append("public API changes must be reflected in the Go wrapper or its tests")
    return errors


def print_changes(changes: list[Change]) -> None:
    if not changes:
        print("    No public header code changes detected.")
        return
    for change in changes:
        print(f"    [{change.severity.label}] {change.kind} {change.name}: {change.detail}")


def summarize_changes(changes: list[Change]) -> str:
    counts = {
        severity: sum(change.severity == severity for change in changes)
        for severity in (Severity.MAJOR, Severity.MINOR)
    }
    return f"{len(changes)} total ({counts[Severity.MAJOR]} MAJOR, {counts[Severity.MINOR]} MINOR)"


def print_report(
    pr_changes: list[Change],
    pr_required: Severity,
    release_ref: str,
    release_changes: list[Change],
    release_version: Version,
    development_version: Version,
    candidate_version: Version | None,
    release_required: Severity,
    declared: Severity | None,
) -> None:
    print("ASTL public API policy report")
    print("  Changes in this PR:")
    print_changes(pr_changes)
    print(f"  PR header requirement: {pr_required.label}")
    if declared is not None:
        print(f"  PR declaration: {declared.label}")
    print(f"  Changes since {release_ref}: {summarize_changes(release_changes)}")
    print(f"  Release version requirement: {release_required.label}")
    print(f"  Current released version in VERSION.md: {development_version.original}")
    if candidate_version is None:
        print("  Candidate release version: not validated in PR CI")
    else:
        print(f"  Candidate release version: {release_version.original} -> {candidate_version.original}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base-ref", required=True, help="Git ref for the target branch or exact baseline")
    parser.add_argument("--base-is-exact", action="store_true", help="Do not resolve the merge base of --base-ref")
    parser.add_argument(
        "--release-ref",
        help="Stable release baseline; defaults to the highest release/X.Y.Z or releases/X.Y.Z tag at or below VERSION.md",
    )
    parser.add_argument(
        "--release-version",
        help="Candidate MAJOR.MINOR.PATCH version to validate for a stable release",
    )
    parser.add_argument("--event-path", type=Path, help="GitHub event JSON; defaults to GITHUB_EVENT_PATH")
    parser.add_argument(
        "--skip-pr-policy", action="store_true", help="Skip PR-body, changelog, and wrapper-touch policy"
    )
    args = parser.parse_args(argv)

    try:
        pr_baseline = args.base_ref if args.base_is_exact else merge_base(args.base_ref)
        development_version = read_version(None)
        candidate_version = Version.parse(args.release_version) if args.release_version else None
        release_ref = args.release_ref or latest_release_ref(
            development_version,
            exclude_version=candidate_version,
        )
        pr_snapshot = build_snapshot(pr_baseline)
        release_snapshot = build_snapshot(release_ref)
        new_snapshot = build_snapshot()
        pr_changes = compare_snapshots(pr_snapshot, new_snapshot)
        release_changes = compare_snapshots(release_snapshot, new_snapshot)
        pr_header_requirement = required_severity(pr_changes)
        release_header_requirement = required_severity(release_changes)
        release_version = read_version(release_ref)

        event_path = args.event_path
        if event_path is None and os.environ.get("GITHUB_EVENT_PATH"):
            event_path = Path(os.environ["GITHUB_EVENT_PATH"])
        declared, declaration_errors = (None, []) if args.skip_pr_policy else parse_pr_declaration(event_path)
        changelog_text = (REPO_ROOT / "CHANGELOG.md").read_text(encoding="utf-8")
        changelog_requirement = unreleased_changelog_severity(changelog_text)
        if candidate_version is not None:
            changelog_requirement = max(
                changelog_requirement,
                changelog_section_severity(changelog_text, candidate_version.original),
            )
        release_requirement = max(
            release_header_requirement,
            changelog_requirement,
            declared or Severity.NONE,
        )
        print_report(
            pr_changes,
            pr_header_requirement,
            release_ref,
            release_changes,
            release_version,
            development_version,
            candidate_version,
            release_requirement,
            declared,
        )

        errors = list(declaration_errors)
        errors.extend(
            validate_candidate_version(
                release_ref,
                release_version,
                candidate_version,
                release_requirement,
            )
        )
        if not args.skip_pr_policy:
            files = changed_files(pr_baseline)
            errors.extend(validate_repository_policy(pr_baseline, pr_header_requirement, declared, files))

        if errors:
            sys.stdout.flush()
            print("API policy check failed:", file=sys.stderr)
            for error in errors:
                print(f"  - {error}", file=sys.stderr)
            return 1
        print("API policy check passed.")
        return 0
    except (OSError, ValueError, RuntimeError, json.JSONDecodeError) as error:
        print(f"API policy check could not run: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
