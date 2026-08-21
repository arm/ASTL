#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

"""Prepare tracked release metadata for review and commit.

Use this script when preparing a stable release commit. It moves the contents of
``CHANGELOG.md``'s ``Unreleased`` section into a dated section for the released
version, resets ``Unreleased``, and writes the requested value to ``VERSION.md``.
With ``--check``, it verifies that those metadata changes have already been
committed without modifying the source tree.

The Prepare Release workflow uses the editing mode to open the first-phase
metadata PR. The Release workflow uses ``--check`` in the second phase before it
publishes the reviewed commit. Run the script directly when preparing or
validating the same metadata locally.

This script only edits source-controlled metadata; it does not build, test, or
package ASTL. Use ``stage_release.sh`` after metadata preparation when you want
to build, test, and stage release artifacts without modifying repository files.
"""

from __future__ import annotations

import argparse
import datetime as dt
import re
import sys
from pathlib import Path


STABLE_VERSION = re.compile(r"^\d+\.\d+\.\d+$")
DEVELOPMENT_VERSION = re.compile(r"^\d+\.\d+\.\d+(?:\.post)?$")
UNRELEASED_HEADING = "## Unreleased"
UNRELEASED_TEMPLATE = """\
## Unreleased

### Added

### Changed

### Breaking
"""


class ReleasePreparationError(ValueError):
    """Raised when release metadata cannot be updated safely."""


def validate_version_pair(release_version: str, version: str) -> None:
    """Validate release and VERSION.md formats and require a shared base version."""
    if not STABLE_VERSION.fullmatch(release_version):
        raise ReleasePreparationError(
            "release version must use MAJOR.MINOR.PATCH format"
        )
    if not DEVELOPMENT_VERSION.fullmatch(version):
        raise ReleasePreparationError(
            "VERSION.md value must use MAJOR.MINOR.PATCH or MAJOR.MINOR.PATCH.post format"
        )
    if version.removesuffix(".post") != release_version:
        raise ReleasePreparationError(
            f"VERSION.md value {version!r} does not represent release {release_version!r}"
        )


def prepare_changelog(text: str, release_version: str, release_date: dt.date) -> str:
    """Move the Unreleased changelog contents into a dated release section."""
    if not STABLE_VERSION.fullmatch(release_version):
        raise ReleasePreparationError(
            "release version must use MAJOR.MINOR.PATCH format"
        )

    lines = text.splitlines()
    unreleased_indexes = [
        index for index, line in enumerate(lines) if line.strip() == UNRELEASED_HEADING
    ]
    if len(unreleased_indexes) != 1:
        raise ReleasePreparationError(
            "CHANGELOG.md must contain exactly one '## Unreleased' heading"
        )

    release_heading = f"## {release_version} - {release_date.isoformat()}"
    if any(
        line.strip() == release_heading
        or line.strip().startswith(f"## {release_version} ")
        for line in lines
    ):
        raise ReleasePreparationError(
            f"CHANGELOG.md already contains a section for {release_version}"
        )

    start = unreleased_indexes[0]
    end = next(
        (
            index
            for index in range(start + 1, len(lines))
            if lines[index].startswith("## ")
        ),
        len(lines),
    )
    unreleased_contents = "\n".join(lines[start + 1 : end]).strip()

    prefix = "\n".join(lines[:start]).rstrip()
    suffix = "\n".join(lines[end:]).strip()
    released_section = release_heading
    if unreleased_contents:
        released_section += f"\n\n{unreleased_contents}"

    sections = [prefix, UNRELEASED_TEMPLATE.rstrip(), released_section]
    if suffix:
        sections.append(suffix)
    return "\n\n".join(section for section in sections if section) + "\n"


def prepare_release(
    repo_root: Path,
    release_version: str,
    version: str,
    release_date: dt.date,
) -> None:
    """Update CHANGELOG.md and VERSION.md beneath ``repo_root``."""
    validate_version_pair(release_version, version)

    changelog_path = repo_root / "CHANGELOG.md"
    version_path = repo_root / "VERSION.md"
    changelog = changelog_path.read_text(encoding="utf-8")
    updated_changelog = prepare_changelog(changelog, release_version, release_date)

    changelog_path.write_text(updated_changelog, encoding="utf-8")
    version_path.write_text(f"{version}\n", encoding="utf-8")


def validate_prepared_release(
    repo_root: Path, release_version: str, version: str
) -> None:
    """Verify that tracked metadata is ready to publish as a stable release."""
    validate_version_pair(release_version, version)

    changelog_path = repo_root / "CHANGELOG.md"
    version_path = repo_root / "VERSION.md"
    recorded_version = version_path.read_text(encoding="utf-8").strip()
    if recorded_version != version:
        raise ReleasePreparationError(
            f"VERSION.md contains {recorded_version!r}; expected {version!r}"
        )

    lines = changelog_path.read_text(encoding="utf-8").splitlines()
    unreleased_indexes = [
        index for index, line in enumerate(lines) if line.strip() == UNRELEASED_HEADING
    ]
    if len(unreleased_indexes) != 1:
        raise ReleasePreparationError(
            "CHANGELOG.md must contain exactly one '## Unreleased' heading"
        )

    start = unreleased_indexes[0]
    end = next(
        (
            index
            for index in range(start + 1, len(lines))
            if lines[index].startswith("## ")
        ),
        len(lines),
    )
    expected_unreleased = UNRELEASED_TEMPLATE.removeprefix(
        f"{UNRELEASED_HEADING}\n"
    ).strip()
    actual_unreleased = "\n".join(lines[start + 1 : end]).strip()
    if actual_unreleased != expected_unreleased:
        raise ReleasePreparationError(
            "CHANGELOG.md Unreleased section is not reset; run prepare_release.py before publishing"
        )

    release_heading = re.compile(
        rf"^## {re.escape(release_version)} - \d{{4}}-\d{{2}}-\d{{2}}$"
    )
    if sum(release_heading.fullmatch(line.strip()) is not None for line in lines) != 1:
        raise ReleasePreparationError(
            f"CHANGELOG.md must contain exactly one dated section for {release_version}"
        )


def parse_date(value: str) -> dt.date:
    """Parse an ISO-8601 calendar date for argparse."""
    try:
        return dt.date.fromisoformat(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("date must use YYYY-MM-DD format") from error


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""\
Choosing a release script:
  prepare_release.py  edits CHANGELOG.md and VERSION.md for a release commit.
  stage_release.sh    builds, tests, and packages an already-prepared source tree.
""",
    )
    parser.add_argument(
        "--release-version",
        required=True,
        help="Stable version recorded in CHANGELOG.md",
    )
    parser.add_argument(
        "--version",
        help="Value written to VERSION.md (default: --release-version; may end in .post)",
    )
    parser.add_argument(
        "--date",
        type=parse_date,
        default=dt.datetime.now(dt.timezone.utc).date(),
        help="Release date in YYYY-MM-DD format (default: current UTC date)",
    )
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=Path(__file__).resolve().parents[2],
        help="Repository root containing CHANGELOG.md and VERSION.md",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Verify prepared metadata without modifying CHANGELOG.md or VERSION.md",
    )
    args = parser.parse_args(argv)

    try:
        repo_root = args.repo_root.resolve()
        version = args.version or args.release_version
        if args.check:
            validate_prepared_release(repo_root, args.release_version, version)
        else:
            prepare_release(repo_root, args.release_version, version, args.date)
    except (OSError, ReleasePreparationError) as error:
        print(f"Release metadata preparation failed: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
