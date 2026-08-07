# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
# SPDX-License-Identifier: Apache-2.0

import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "release" / "stage_release.sh"


def test_help_describes_public_defaults() -> None:
    result = subprocess.run(["bash", str(SCRIPT), "--help"], check=True, capture_output=True, text=True)

    assert "No overlay is required" in result.stdout
    assert "default: library-only" in result.stdout
    assert "--confidential-config" in result.stdout


def test_unknown_variant_fails_before_build() -> None:
    result = subprocess.run(
        ["bash", str(SCRIPT), "--variant", "unknown"],
        check=False,
        capture_output=True,
        text=True,
    )

    assert result.returncode == 2
    assert "unsupported variant" in result.stderr
