# SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

import subprocess
import sys
import tarfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PYDIR = ROOT / "python"
DIST = PYDIR / "dist"


def build_wheel():
    if DIST.exists():
        for f in DIST.iterdir():
            f.unlink()
    subprocess.run([sys.executable, "-m", "build", "--wheel", str(PYDIR)], check=True, stdout=subprocess.DEVNULL)
    wheels = list(DIST.glob("astl-*.whl"))
    assert wheels, "No wheel produced"
    return wheels[0]


def test_wheel_contains_license():
    wheel = build_wheel()
    # Wheel is a zip archive.
    with zipfile.ZipFile(wheel) as zf:
        namelist = zf.namelist()
        # Accept either top-level LICENSE copied or one inside astl/ depending on packaging
        license_candidates = [n for n in namelist if n.endswith("LICENSE")]
        assert license_candidates, f"No LICENSE file found in wheel. Entries: {namelist[:20]}"
        contents = {name: zf.read(name).decode("utf-8", errors="replace") for name in license_candidates}
        assert any("Apache" in text and "2.0" in text for text in contents.values()), "LICENSE content does not look like Apache-2.0"


def test_sdist_contains_license():
    # Build sdist too and check
    if DIST.exists():
        for f in DIST.iterdir():
            f.unlink()
    subprocess.run([sys.executable, "-m", "build", "--sdist", str(PYDIR)], check=True, stdout=subprocess.DEVNULL)
    sdists = list(DIST.glob("astl-*.tar.gz"))
    assert sdists, "No sdist produced"
    sdist = sdists[0]
    with tarfile.open(sdist, "r:gz") as tf:
        names = tf.getnames()
        license_candidates = [n for n in names if n.endswith("/LICENSE") or n.endswith("LICENSE")]
        assert license_candidates, "No LICENSE file found in sdist"
        # Optionally validate content for Apache license phrase
        found_apache = False
        for member in license_candidates:
            f = tf.extractfile(member)
            if not f:
                continue
            text = f.read().decode("utf-8", errors="replace")
            if "Apache License" in text and "Version 2.0" in text:
                found_apache = True
                break
        assert found_apache, "LICENSE in sdist does not appear to contain Apache-2.0 text"
