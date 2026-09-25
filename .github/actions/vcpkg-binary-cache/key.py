# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

"""Compute stable, content-based vcpkg binary archive keys across workflows."""

import hashlib
import os
from pathlib import Path
import platform
import re
import subprocess


def version(command: str) -> str:
    try:
        result = subprocess.run([command, "--version"], check=True, capture_output=True, text=True)
        return result.stdout.splitlines()[0]
    except (FileNotFoundError, subprocess.CalledProcessError, IndexError):
        return f"{command}:unavailable"


def digest_sources(source_dir: Path) -> str:
    manifest = source_dir / "vcpkg.json"
    if not manifest.is_file():
        raise SystemExit(f"Missing vcpkg manifest: {manifest}")

    files = [manifest]
    configuration = source_dir / "vcpkg-configuration.json"
    if configuration.is_file():
        files.append(configuration)
    for directory in ("cmake/triplets", "cmake/toolchains"):
        files.extend(path for path in (source_dir / directory).rglob("*") if path.is_file())

    digest = hashlib.sha256()
    for path in sorted(files, key=lambda item: item.relative_to(source_dir).as_posix()):
        digest.update(path.relative_to(source_dir).as_posix().encode())
        digest.update(b"\0")
        digest.update(path.read_bytes())
        digest.update(b"\0")
    return digest.hexdigest()[:24]


def main() -> None:
    cohort = os.environ["ASTL_CACHE_COHORT"]
    if not re.fullmatch(r"[a-z0-9-]+", cohort):
        raise SystemExit(f"Invalid vcpkg cache cohort: {cohort}")

    source_dir = Path(os.environ["ASTL_CACHE_SOURCE_DIR"]).resolve()
    compiler = os.environ["ASTL_CACHE_COMPILER"]
    sources = digest_sources(source_dir)
    tools = "\n".join(
        (
            platform.system(),
            platform.machine(),
            os.environ.get("ImageVersion", "self-hosted"),
            version(compiler),
            version("cmake"),
        )
    )
    tool_digest = hashlib.sha256(tools.encode()).hexdigest()[:24]
    prefix = f"vcpkg-{cohort}-v1-{sources}-"
    print(f"key={prefix}{tool_digest}")
    print(f"restore-prefix={prefix}")
    print(f"path={Path(os.environ['GITHUB_WORKSPACE']) / '.vcpkg-cache'}")


if __name__ == "__main__":
    main()
