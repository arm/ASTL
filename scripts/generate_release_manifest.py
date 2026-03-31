#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

"""
This script generates a manifest for an ASTL release package. The manifest includes metadata about the package, 
a list of files with their hashes and classifications, and information about installer artifacts. 
The manifest is intended to be used by the ASTL bootstrap installer to verify the integrity of the install package,
and by the uninstall script to know which  files to remove.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def classify_destination(relative_path: str) -> dict[str, str] | None:
    if relative_path.startswith("lib/config/"):
        return {
            "kind": "config",
            "relative_path": relative_path.removeprefix("lib/config/"),
            "mode": "0644",
        }

    if relative_path.startswith("include/"):
        return {
            "kind": "include",
            "relative_path": relative_path.removeprefix("include/"),
            "mode": "0644",
        }

    if relative_path.startswith("lib/"):
        return {
            "kind": "lib",
            "relative_path": relative_path.removeprefix("lib/"),
            "mode": "0644",
        }

    if relative_path.startswith("bin/"):
        return {
            "kind": "bin",
            "relative_path": relative_path.removeprefix("bin/"),
            "mode": "0755",
        }

    # The first implementation intentionally keeps installation focused on the
    # runtime layout from the release plan. Samples remain packaged
    # and verified, but are not installed by the bootstrap script yet.
    return None


def build_manifest(staging_dir: Path, version: str, os_name: str, arch: str, variant: str) -> dict:
    entries: list[dict] = []

    for path in sorted(staging_dir.rglob("*")):
        if path.name == "manifest.json":
            continue
        if path.is_dir():
            continue

        relative_path = path.relative_to(staging_dir).as_posix()
        destination = classify_destination(relative_path)
        entries.append({
            "path": relative_path,
            "type": "file",
            "sha256": sha256_file(path),
            "destination": destination,
        })

    optional_commands = []
    if variant == "everything":
        optional_commands.append(
            {
                "name": "gnuplot",
                "reason": "Optional ATX plot report types require gnuplot.",
            }
        )

    installer_artifacts = [
        {
            "kind": "copy",
            "source_path": "VERSION.md",
            "destination": {
                "kind": "install_state",
                "relative_path": "VERSION.md",
                "mode": "0644",
            },
        },
        {
            "kind": "copy",
            "source_path": "manifest.json",
            "destination": {
                "kind": "install_state",
                "relative_path": "manifest.json",
                "mode": "0644",
            },
        },
        {
            "kind": "copy",
            "source_path": "uninstall.sh",
            "destination": {
                "kind": "install_state",
                "relative_path": "uninstall.sh",
                "mode": "0755",
            },
        },
        {
            "kind": "wrapper",
            "destination": {
                "kind": "bin",
                "relative_path": "astl-uninstall",
                "mode": "0755",
            },
            "target": {
                "kind": "install_state",
                "relative_path": "uninstall.sh",
            },
        },
    ]

    return {
        "schema_version": 1,
        "package": {
            "name": "astl",
            "version": version,
            "os": os_name,
            "arch": arch,
            "variant": variant,
        },
        "runtime_dependencies": {
            "required_commands": ["curl", "unzip", "sha256sum", "python3"],
            "optional_commands": optional_commands,
        },
        "package_files": entries,
        "installer_artifacts": installer_artifacts,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate a release manifest for an ASTL staging directory.")
    parser.add_argument("--staging-dir", required=True, type=Path)
    parser.add_argument("--version", required=True)
    parser.add_argument("--os", required=True, dest="os_name")
    parser.add_argument("--arch", required=True)
    parser.add_argument("--variant", required=True)
    args = parser.parse_args()

    manifest = build_manifest(args.staging_dir, args.version, args.os_name, args.arch, args.variant)
    output_path = args.staging_dir / "manifest.json"
    output_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
