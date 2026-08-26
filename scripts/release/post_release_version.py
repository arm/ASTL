#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

"""Decide whether main needs a monotonic post-release VERSION.md update."""

from __future__ import annotations

import argparse
import re


VERSION = re.compile(r"^(\d+)\.(\d+)\.(\d+)(\.post)?$")


def parse_version(value: str) -> tuple[tuple[int, int, int], bool]:
    match = VERSION.fullmatch(value)
    if match is None:
        raise ValueError(
            f"invalid version {value!r}; expected MAJOR.MINOR.PATCH or MAJOR.MINOR.PATCH.post"
        )
    base = (int(match.group(1)), int(match.group(2)), int(match.group(3)))
    return base, match.group(4) is not None


def post_release_action(current: str, released: str) -> str:
    current_base, current_is_post = parse_version(current)
    released_base, released_is_post = parse_version(released)
    if released_is_post:
        raise ValueError("released version must not have a .post suffix")
    if current_base > released_base:
        return "skip"
    if current_base == released_base and current_is_post:
        return "skip"
    return "update"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--current", required=True)
    parser.add_argument("--released", required=True)
    args = parser.parse_args()
    try:
        print(post_release_action(args.current, args.released))
    except ValueError as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
