#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import datetime as dt
import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).resolve().parents[1] / "release" / "prepare_release.py"
SPEC = importlib.util.spec_from_file_location("prepare_release", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
release = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = release
SPEC.loader.exec_module(release)


CHANGELOG = """\
# ASTL Changelog

## Unreleased

### Added

- New API

### Changed

- Updated behavior

### Breaking

## 1.0.0 - 2026-01-01

### Added

- Initial release
"""


class PrepareChangelogTests(unittest.TestCase):
    def test_moves_unreleased_contents_and_resets_sections(self) -> None:
        updated = release.prepare_changelog(CHANGELOG, "1.1.0", dt.date(2026, 8, 20))

        unreleased, released = updated.split("## 1.1.0 - 2026-08-20", maxsplit=1)
        self.assertEqual(unreleased.count("## Unreleased"), 1)
        self.assertNotIn("New API", unreleased)
        self.assertNotIn("Updated behavior", unreleased)
        self.assertIn("### Added\n\n### Changed\n\n### Breaking", unreleased)
        self.assertIn("- New API", released)
        self.assertIn("- Updated behavior", released)
        self.assertIn("## 1.0.0 - 2026-01-01", released)

    def test_rejects_duplicate_release_section(self) -> None:
        with self.assertRaisesRegex(
            release.ReleasePreparationError, "already contains"
        ):
            release.prepare_changelog(CHANGELOG, "1.0.0", dt.date(2026, 8, 20))

    def test_requires_exactly_one_unreleased_section(self) -> None:
        with self.assertRaisesRegex(release.ReleasePreparationError, "exactly one"):
            release.prepare_changelog(
                "# ASTL Changelog\n", "1.1.0", dt.date(2026, 8, 20)
            )


class PrepareReleaseTests(unittest.TestCase):
    def test_updates_changelog_and_version_together(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            repo_root = Path(directory)
            (repo_root / "CHANGELOG.md").write_text(CHANGELOG, encoding="utf-8")
            (repo_root / "VERSION.md").write_text("1.0.0\n", encoding="utf-8")

            release.prepare_release(
                repo_root, "1.1.0", "1.1.0.post", dt.date(2026, 8, 20)
            )

            self.assertIn(
                "## 1.1.0 - 2026-08-20",
                (repo_root / "CHANGELOG.md").read_text(encoding="utf-8"),
            )
            self.assertEqual(
                (repo_root / "VERSION.md").read_text(encoding="utf-8"), "1.1.0.post\n"
            )
            release.validate_prepared_release(repo_root, "1.1.0", "1.1.0.post")

    def test_validation_rejects_unpromoted_changelog(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            repo_root = Path(directory)
            (repo_root / "CHANGELOG.md").write_text(CHANGELOG, encoding="utf-8")
            (repo_root / "VERSION.md").write_text("1.1.0\n", encoding="utf-8")

            with self.assertRaisesRegex(release.ReleasePreparationError, "not reset"):
                release.validate_prepared_release(repo_root, "1.1.0", "1.1.0")

    def test_validation_rejects_mismatched_version(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            repo_root = Path(directory)
            changelog = release.prepare_changelog(
                CHANGELOG, "1.1.0", dt.date(2026, 8, 20)
            )
            (repo_root / "CHANGELOG.md").write_text(changelog, encoding="utf-8")
            (repo_root / "VERSION.md").write_text("1.1.0.post\n", encoding="utf-8")

            with self.assertRaisesRegex(
                release.ReleasePreparationError, "VERSION.md contains"
            ):
                release.validate_prepared_release(repo_root, "1.1.0", "1.1.0")

    def test_rejects_invalid_version_file_value_before_writing(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            repo_root = Path(directory)
            changelog_path = repo_root / "CHANGELOG.md"
            version_path = repo_root / "VERSION.md"
            changelog_path.write_text(CHANGELOG, encoding="utf-8")
            version_path.write_text("1.0.0\n", encoding="utf-8")

            with self.assertRaisesRegex(
                release.ReleasePreparationError, "VERSION.md value"
            ):
                release.prepare_release(
                    repo_root, "1.1.0", "next", dt.date(2026, 8, 20)
                )

            self.assertEqual(changelog_path.read_text(encoding="utf-8"), CHANGELOG)
            self.assertEqual(version_path.read_text(encoding="utf-8"), "1.0.0\n")

    def test_rejects_unrelated_stable_and_post_versions_before_writing(self) -> None:
        for version in ("9.4.3", "9.4.3.post"):
            with (
                self.subTest(version=version),
                tempfile.TemporaryDirectory() as directory,
            ):
                repo_root = Path(directory)
                changelog_path = repo_root / "CHANGELOG.md"
                version_path = repo_root / "VERSION.md"
                changelog_path.write_text(CHANGELOG, encoding="utf-8")
                version_path.write_text("1.0.0\n", encoding="utf-8")

                with self.assertRaisesRegex(
                    release.ReleasePreparationError,
                    "does not represent release",
                ):
                    release.prepare_release(
                        repo_root,
                        "1.1.0",
                        version,
                        dt.date(2026, 8, 20),
                    )

                self.assertEqual(changelog_path.read_text(encoding="utf-8"), CHANGELOG)
                self.assertEqual(version_path.read_text(encoding="utf-8"), "1.0.0\n")

    def test_validation_rejects_unrelated_stable_and_post_versions(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            repo_root = Path(directory)
            changelog = release.prepare_changelog(
                CHANGELOG, "1.1.0", dt.date(2026, 8, 20)
            )
            (repo_root / "CHANGELOG.md").write_text(changelog, encoding="utf-8")
            version_path = repo_root / "VERSION.md"

            for version in ("9.4.3", "9.4.3.post"):
                with self.subTest(version=version):
                    version_path.write_text(f"{version}\n", encoding="utf-8")
                    with self.assertRaisesRegex(
                        release.ReleasePreparationError,
                        "does not represent release",
                    ):
                        release.validate_prepared_release(
                            repo_root,
                            "1.1.0",
                            version,
                        )


if __name__ == "__main__":
    unittest.main()
