#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch
from pathlib import Path


SCRIPT = Path(__file__).resolve().parents[1] / "check_api_policy.py"
SPEC = importlib.util.spec_from_file_location("check_api_policy", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
policy = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = policy
SPEC.loader.exec_module(policy)


class SnapshotComparisonTests(unittest.TestCase):
    def test_function_addition_requires_minor(self) -> None:
        old = policy.ApiSnapshot()
        new = policy.ApiSnapshot(functions={"astlNew": ("int (void)", ())})
        changes = policy.compare_snapshots(old, new)
        self.assertEqual(policy.required_severity(changes), policy.Severity.MINOR)

    def test_function_parameter_name_change_requires_no_version_bump(self) -> None:
        def function_node(parameter_name: str) -> dict:
            return {
                "kind": "FunctionDecl",
                "name": "astlRead",
                "type": {"qualType": "int (int)"},
                "inner": [
                    {
                        "kind": "ParmVarDecl",
                        "name": parameter_name,
                        "type": {"qualType": "int"},
                    }
                ],
            }

        old = policy.parse_ast({"inner": [function_node("value")]})
        new = policy.parse_ast({"inner": [function_node("input")]})
        self.assertEqual(policy.compare_snapshots(old, new), [])

    def test_function_parameter_type_change_requires_major(self) -> None:
        old = policy.ApiSnapshot(functions={"astlRead": ("int (int)", ("int",))})
        new = policy.ApiSnapshot(functions={"astlRead": ("int (long)", ("long",))})
        changes = policy.compare_snapshots(old, new)
        self.assertEqual(policy.required_severity(changes), policy.Severity.MAJOR)

    def test_function_export_or_linkage_change_requires_major(self) -> None:
        functions = {"astlStatusString": ("const char *(int)", ("int",))}
        original = policy.ApiSnapshot(
            functions=functions,
            function_abi={"astlStatusString": ("C", True)},
        )

        for changed_abi in (("C", False), ("C++", True)):
            with self.subTest(changed_abi=changed_abi):
                changed = policy.ApiSnapshot(
                    functions=functions,
                    function_abi={"astlStatusString": changed_abi},
                )
                changes = policy.compare_snapshots(original, changed)
                self.assertEqual(changes[0].severity, policy.Severity.MAJOR)
                self.assertEqual(changes[0].kind, "function ABI")

    def test_cpp_ast_fingerprints_export_and_c_linkage(self) -> None:
        function = {
            "kind": "FunctionDecl",
            "name": "astlStatusString",
            "inner": [{"kind": "VisibilityAttr", "visibility": "default"}],
        }
        ast = {
            "kind": "TranslationUnitDecl",
            "inner": [{"kind": "LinkageSpecDecl", "language": "C", "inner": [function]}],
        }
        self.assertEqual(
            policy.extract_function_abi(ast),
            {"astlStatusString": ("C", True)},
        )

    def test_versioned_struct_tail_extension_requires_minor(self) -> None:
        old_fields = (("size", "size_t", False, ""), ("flags", "uint32_t", False, ""))
        new_fields = old_fields + (("timeout", "uint64_t", False, ""),)
        old = policy.ApiSnapshot(records={"astl_params_t": ("struct", old_fields)})
        new = policy.ApiSnapshot(records={"astl_params_t": ("struct", new_fields)})
        changes = policy.compare_snapshots(old, new)
        self.assertEqual(changes[0].severity, policy.Severity.MINOR)
        self.assertIn("timeout", changes[0].detail)

    def test_unversioned_struct_tail_extension_requires_major(self) -> None:
        old_fields = (("timestamp", "uint64_t", False, ""), ("value", "uint64_t", False, ""))
        new_fields = old_fields + (("status", "uint32_t", False, ""),)
        old = policy.ApiSnapshot(records={"astl_sample_t": ("struct", old_fields)})
        new = policy.ApiSnapshot(records={"astl_sample_t": ("struct", new_fields)})

        changes = policy.compare_snapshots(old, new)

        self.assertEqual(changes[0].severity, policy.Severity.MAJOR)
        self.assertIn("without leading size", changes[0].detail)
        self.assertIn("status", changes[0].detail)

    def test_nonleading_size_does_not_make_tail_extension_compatible(self) -> None:
        old_fields = (("value", "uint64_t", False, ""), ("size", "size_t", False, ""))
        new_fields = old_fields + (("status", "uint32_t", False, ""),)

        change = policy.classify_record_change(
            "astl_unversioned_t",
            ("struct", old_fields),
            ("struct", new_fields),
        )

        self.assertIsNotNone(change)
        assert change is not None
        self.assertEqual(change.severity, policy.Severity.MAJOR)

    def test_struct_reorder_and_union_extension_require_major(self) -> None:
        fields = (("first", "int", False, ""), ("second", "long", False, ""))
        reordered = tuple(reversed(fields))
        old = policy.ApiSnapshot(
            records={"astl_record_t": ("struct", fields), "_astl_value_t": ("union", fields[:1])}
        )
        new = policy.ApiSnapshot(
            records={"astl_record_t": ("struct", reordered), "_astl_value_t": ("union", fields)}
        )
        changes = policy.compare_snapshots(old, new)
        self.assertTrue(all(change.severity == policy.Severity.MAJOR for change in changes))

    def test_enum_append_before_reserved_terminal_requires_minor(self) -> None:
        old_values = (("ASTL_STATUS_SUCCESS", "0"), ("ASTL_STATUS_INTERNAL_ERROR", "127"))
        new_values = (
            ("ASTL_STATUS_SUCCESS", "0"),
            ("ASTL_STATUS_NEW_FEATURE", "1"),
            ("ASTL_STATUS_INTERNAL_ERROR", "127"),
        )
        old = policy.ApiSnapshot(enums={"_astl_status_code": old_values})
        new = policy.ApiSnapshot(enums={"_astl_status_code": new_values})
        changes = policy.compare_snapshots(old, new)
        self.assertEqual(changes[0].severity, policy.Severity.MINOR)

    def test_enum_reorder_or_value_change_requires_major(self) -> None:
        old = policy.ApiSnapshot(enums={"_astl_mode_t": (("ASTL_A", "0"), ("ASTL_B", "1"))})
        new = policy.ApiSnapshot(enums={"_astl_mode_t": (("ASTL_B", "1"), ("ASTL_A", "2"))})
        changes = policy.compare_snapshots(old, new)
        self.assertEqual(changes[0].severity, policy.Severity.MAJOR)

    def test_macro_addition_is_minor_but_modification_is_major(self) -> None:
        added = policy.compare_snapshots(
            policy.ApiSnapshot(), policy.ApiSnapshot(macros={"ASTL_NEW(x)": "((x) + 1)"})
        )
        changed = policy.compare_snapshots(
            policy.ApiSnapshot(macros={"ASTL_VALUE": "1"}),
            policy.ApiSnapshot(macros={"ASTL_VALUE": "2"}),
        )
        self.assertEqual(added[0].severity, policy.Severity.MINOR)
        self.assertEqual(changed[0].severity, policy.Severity.MAJOR)


class VersionPolicyTests(unittest.TestCase):
    def test_version_parser_accepts_post_suffix(self) -> None:
        version = policy.Version.parse("0.4.2.post\n")
        self.assertEqual((version.major, version.minor, version.patch), (0, 4, 2))

    def test_minimum_required_bumps(self) -> None:
        base = policy.Version.parse("1.2.3")
        self.assertTrue(policy.version_satisfies(base, policy.Version.parse("1.3.0"), policy.Severity.MINOR))
        self.assertTrue(policy.version_satisfies(base, policy.Version.parse("2.0.0"), policy.Severity.MINOR))
        self.assertFalse(policy.version_satisfies(base, policy.Version.parse("1.2.4"), policy.Severity.MINOR))
        self.assertTrue(policy.version_satisfies(base, policy.Version.parse("2.0.0"), policy.Severity.MAJOR))
        self.assertFalse(policy.version_satisfies(base, policy.Version.parse("1.9.0"), policy.Severity.MAJOR))

    def test_release_candidate_is_compared_with_stable_release(self) -> None:
        release = policy.Version.parse("1.2.3")
        candidate = policy.Version.parse("2.0.0")

        self.assertEqual(
            policy.validate_candidate_version("release/1.2.3", release, candidate, policy.Severity.MAJOR),
            [],
        )

    def test_pr_check_does_not_require_a_candidate_release_version(self) -> None:
        errors = policy.validate_candidate_version(
            "release/1.2.3",
            policy.Version.parse("1.2.3"),
            None,
            policy.Severity.MAJOR,
        )
        self.assertEqual(errors, [])

    def test_release_candidate_must_satisfy_cumulative_requirement(self) -> None:
        errors = policy.validate_candidate_version(
            "release/1.2.3",
            policy.Version.parse("1.2.3"),
            policy.Version.parse("1.3.0"),
            policy.Severity.MAJOR,
        )
        self.assertEqual(
            errors,
            ["MAJOR change requires a higher major version than release release/1.2.3 (1.2.3)"],
        )

    def test_release_change_summary_counts_each_severity(self) -> None:
        changes = [
            policy.Change(policy.Severity.MAJOR, "function", "astlOld", "removed"),
            policy.Change(policy.Severity.MINOR, "function", "astlNew", "added"),
            policy.Change(policy.Severity.MINOR, "struct", "astl_params_t", "added"),
        ]
        self.assertEqual(policy.summarize_changes(changes), "3 total (1 MAJOR, 2 MINOR)")

    def test_unreleased_changelog_preserves_semantic_change_severity(self) -> None:
        changelog = """\
## Unreleased
### Added
- Compatible user flow
### Breaking
- Incompatible user flow
## 1.0.0
### Breaking
- Older break
"""
        self.assertEqual(policy.unreleased_changelog_severity(changelog), policy.Severity.MAJOR)

    def test_released_changelog_entries_do_not_affect_next_release(self) -> None:
        changelog = """\
## Unreleased
### Added
### Breaking
## 1.0.0
### Breaking
- Older break
"""
        self.assertEqual(policy.unreleased_changelog_severity(changelog), policy.Severity.NONE)

    def test_candidate_release_section_preserves_semantic_change_severity(self) -> None:
        changelog = """\
## Unreleased
### Added
### Breaking
## 2.0.0 - 2026-08-20
### Breaking
- Incompatible user flow
"""
        self.assertEqual(
            policy.changelog_section_severity(changelog, "2.0.0"),
            policy.Severity.MAJOR,
        )

    @patch.object(
        policy,
        "run",
        return_value="release/rolling\nreleases/0.0.1\nrelease/1.2.0\nrelease/1.10.0\nrelease/2.0.0\nnot-a-release\n",
    )
    def test_latest_release_ref_selects_highest_stable_version(self, _run: Mock) -> None:
        self.assertEqual(policy.latest_release_ref(policy.Version.parse("1.10.1.post")), "release/1.10.0")

    @patch.object(
        policy,
        "run",
        return_value="release/1.2.3\nrelease/1.3.0\n",
    )
    def test_latest_release_ref_excludes_candidate_during_retry(self, _run: Mock) -> None:
        self.assertEqual(
            policy.latest_release_ref(
                policy.Version.parse("1.3.0"),
                exclude_version=policy.Version.parse("1.3.0"),
            ),
            "release/1.2.3",
        )

    @patch.object(policy, "run", return_value="release/rolling\n")
    def test_latest_release_ref_requires_a_stable_tag(self, _run: Mock) -> None:
        with self.assertRaisesRegex(RuntimeError, "no stable release tag"):
            policy.latest_release_ref(policy.Version.parse("1.0.0"))


class PullRequestPolicyTests(unittest.TestCase):
    def write_event(self, body: str) -> Path:
        temporary = tempfile.NamedTemporaryFile(mode="w", suffix=".json", delete=False)
        json.dump({"pull_request": {"body": body}}, temporary)
        temporary.close()
        self.addCleanup(Path(temporary.name).unlink)
        return Path(temporary.name)

    def test_exactly_one_declaration_is_required(self) -> None:
        event = self.write_event("- [x] Backward-compatible public API addition\n")
        severity, errors = policy.parse_pr_declaration(event)
        self.assertEqual(severity, policy.Severity.MINOR)
        self.assertEqual(errors, [])

        missing = self.write_event("No declaration")
        severity, errors = policy.parse_pr_declaration(missing)
        self.assertIsNone(severity)
        self.assertTrue(errors)

    @patch.object(policy, "added_changelog_entries", return_value=["- Added API"])
    def test_api_policy_requires_both_wrappers(self, _entries: Mock) -> None:
        files = ["CHANGELOG.md", "python/astl/_core.pyx"]
        errors = policy.validate_repository_policy(
            "base", policy.Severity.MINOR, policy.Severity.MINOR, files
        )
        self.assertEqual(errors, ["public API changes must be reflected in the Go wrapper or its tests"])

    @patch.object(policy, "added_changelog_entries", return_value=["- Breaking API"])
    def test_declared_semantic_break_elevates_header_result(self, _entries: Mock) -> None:
        files = ["CHANGELOG.md", "python/tests/test_api.py", "Go/astl/astl_test.go"]
        errors = policy.validate_repository_policy(
            "base", policy.Severity.NONE, policy.Severity.MAJOR, files
        )
        self.assertEqual(errors, [])


class ClangExtractionTests(unittest.TestCase):
    def test_current_headers_expose_expected_api(self) -> None:
        snapshot = policy.build_snapshot()
        self.assertIn("astlGetTargets", snapshot.functions)
        self.assertEqual(snapshot.function_abi["astlStatusString"], ("C", True))
        self.assertIn("_astl_status_code", snapshot.enums)
        self.assertIn("_astl_target_props_t", snapshot.records)
        self.assertIn("ASTL_INIT_STRUCT", {name.split("(", 1)[0] for name in snapshot.macros})


if __name__ == "__main__":
    unittest.main()
