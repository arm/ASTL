# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
# SPDX-License-Identifier: Apache-2.0

import os
import shutil
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def test_enable_stores_opaque_profile(tmp_path: Path) -> None:
    checkout = tmp_path / "checkout"
    scripts = checkout / "scripts"
    source = tmp_path / "overlay"
    bin_dir = tmp_path / "bin"
    scripts.mkdir(parents=True)
    source.mkdir()
    bin_dir.mkdir()
    shutil.copy2(ROOT / "scripts" / "astl_overlay.sh", scripts / "astl_overlay.sh")
    (source / ".ossmosis.json").write_text('{"schema_version": 1, "path_rules": []}\n', encoding="utf-8")
    fake_ossmosis = bin_dir / "ossmosis"
    fake_ossmosis.write_text("#!/usr/bin/env bash\nexit 0\n", encoding="utf-8")
    fake_ossmosis.chmod(0o755)
    subprocess.run(["git", "init", "-q", str(checkout)], check=True)

    env = os.environ.copy()
    env["PATH"] = f"{bin_dir}{os.pathsep}{env['PATH']}"
    subprocess.run(
        ["bash", str(scripts / "astl_overlay.sh"), "enable", str(source), "test-profile-a"],
        check=True,
        env=env,
    )

    profile = subprocess.run(
        ["git", "-C", str(checkout), "config", "--local", "--get", "astl.overlayProfile"],
        check=True,
        capture_output=True,
        text=True,
    )
    assert profile.stdout.strip() == "test-profile-a"
