# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

import pytest

import astl

def test_get_targets():
    targets = astl.get_targets()
    assert isinstance(targets, list)
    # Targets may be empty; just ensure list semantics


def test_clock_correlation_retains_integer_nanoseconds():
    astl.get_targets()  # Triggers live library initialization.
    reference = astl.get_clock_correlation()
    assert set(reference) == {"raw_ns", "realtime_ns", "uncertainty_ns"}
    assert all(isinstance(value, int) for value in reference.values())
    assert reference["uncertainty_ns"] >= 0
    assert reference["realtime_ns"] > 0


def test_clock_reference_initializes_and_survives_archive_replay(tmp_path):
    import subprocess
    import sys

    # A fresh process verifies lazy initialization and avoids changing other tests' sessions.
    script = """
import astl
import json
import sys
import zipfile

reference = astl.get_clock_correlation()
assert reference['realtime_ns'] > 0
astl.save_collection(sys.argv[1])
astl.load_collection(sys.argv[1])
assert astl.get_clock_correlation() == reference

# Legacy archives must not acquire a reference from the analysis host.
with zipfile.ZipFile(sys.argv[1]) as source, zipfile.ZipFile(sys.argv[2], 'w') as legacy:
    found = False
    for entry in source.infolist():
        data = source.read(entry.filename)
        if entry.filename.endswith('platform_info.astl'):
            metadata = json.loads(data)
            del metadata['wall_clock_correlation']
            data = json.dumps(metadata).encode()
            found = True
        legacy.writestr(entry, data)
    assert found
astl.load_collection(sys.argv[2])
try:
    astl.get_clock_correlation()
except astl.ASTLError:
    pass
else:
    raise AssertionError('Legacy archive acquired a wall clock reference')
"""
    subprocess.run(
        [sys.executable, "-c", script, str(tmp_path / "clock.astl"), str(tmp_path / "legacy.astl")],
        check=True,
        capture_output=True,
        text=True,
        timeout=30,
    )
