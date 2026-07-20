# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

import importlib
import pathlib

def test_package_version_matches_version_file():
    astl = importlib.import_module("astl")
    repo_version = None
    # Ascend to locate VERSION.md (expected at repo root)
    pkg_path = pathlib.Path(astl.__file__).resolve()
    for parent in pkg_path.parents:
        candidate = parent / "VERSION.md"
        if candidate.is_file():
            with candidate.open("r", encoding="utf-8") as f:
                for line in f:
                    line = line.strip()
                    if line and not line.startswith("#"):
                        repo_version = line
                        break
            break
    if repo_version is None:
        # If we cannot find VERSION.md (e.g. unusual install), at least assert a non-empty version
        assert astl.__version__
    else:
        assert astl.__version__ == repo_version
