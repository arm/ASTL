# SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

"""Helper to locate built ASTL shared library for runtime if not installed system-wide.
Currently unused but can be expanded to modify ctypes / LD paths dynamically.
"""
from __future__ import annotations
import os
import pathlib


def candidate_library_paths() -> list[str]:
    root = pathlib.Path(__file__).resolve().parents[2]
    build_dirs = [root / "build" / "debug" / "lib", root / "build" / "release" / "lib"]
    paths = []
    for d in build_dirs:
        if d.exists():
            for name in d.glob("libastl-*.so"):
                paths.append(str(name))
    return paths
