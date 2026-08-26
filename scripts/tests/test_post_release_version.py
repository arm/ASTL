#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import importlib.util
import sys
import unittest
from pathlib import Path


SCRIPT = Path(__file__).resolve().parents[1] / "release" / "post_release_version.py"
SPEC = importlib.util.spec_from_file_location("post_release_version", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
versions = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = versions
SPEC.loader.exec_module(versions)


class PostReleaseVersionTests(unittest.TestCase):
    def test_updates_matching_stable_version(self) -> None:
        self.assertEqual(versions.post_release_action("0.2.0", "0.2.0"), "update")

    def test_skips_matching_post_version(self) -> None:
        self.assertEqual(versions.post_release_action("0.2.0.post", "0.2.0"), "skip")

    def test_never_moves_main_backwards(self) -> None:
        self.assertEqual(versions.post_release_action("0.3.0", "0.2.0"), "skip")
        self.assertEqual(versions.post_release_action("0.3.0.post", "0.2.0"), "skip")

    def test_advances_main_when_it_still_records_the_previous_release(self) -> None:
        self.assertEqual(
            versions.post_release_action("0.1.0.post", "0.2.0"), "update"
        )


if __name__ == "__main__":
    unittest.main()
