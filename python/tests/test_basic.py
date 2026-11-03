# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

import pytest

import astl

def test_get_targets():
    targets = astl.get_targets()
    assert isinstance(targets, list)
    # Targets may be empty; just ensure list semantics


def test_system_info_clock_reference_lifecycle(tmp_path):
    import subprocess
    import sys

    script = """
import astl
import json
import sys
import zipfile

info = astl.get_system_info()
assert info['clock_correlation'] is None
astl.get_targets()
initialized_info = astl.get_system_info()
assert initialized_info['clock_correlation']['clock_reference_utc'].endswith('Z')
initialized_reference = initialized_info['clock_correlation']
assert initialized_reference['realtime_ns'] > 0
assert astl.get_system_info()['clock_correlation'] == initialized_reference
for target in astl.get_targets():
    counters = astl.get_counters(target)
    if counters:
        break
else:
    raise AssertionError('No counter available for collection')
params = astl.CollectionParameters(sampling_interval=10, mode=astl.CollectionMode.IMMEDIATE)
astl.configure_counters_on_target(target, params, [counters[0]])
configured_reference = astl.get_system_info()['clock_correlation']
assert configured_reference is not None
assert configured_reference['raw_ns'] >= initialized_reference['raw_ns']
astl.start_collection(target)
astl.stop_collection(target)
info = astl.get_system_info()
reference = info['clock_correlation']
assert reference == configured_reference
assert set(reference) == {'raw_ns', 'realtime_ns', 'uncertainty_ns', 'clock_reference_utc'}
assert all(isinstance(reference[key], int) for key in ('raw_ns', 'realtime_ns', 'uncertainty_ns'))
assert reference['realtime_ns'] > 0
assert reference['uncertainty_ns'] >= 0
assert info['clock_correlation']['clock_reference_utc'].endswith('Z')
astl.save_collection(sys.argv[1])
astl.load_collection(sys.argv[1])
assert astl.get_system_info()['clock_correlation'] == reference
assert astl.get_system_info()['clock_correlation']['clock_reference_utc'] == info['clock_correlation']['clock_reference_utc']
with zipfile.ZipFile(sys.argv[1]) as source, zipfile.ZipFile(sys.argv[2], 'w') as legacy:
    found = False
    for entry in source.infolist():
        data = source.read(entry.filename)
        if entry.filename.endswith('platform_info.astl'):
            metadata = json.loads(data)
            assert metadata['wall_clock_correlation']['utc'] == info['clock_correlation']['clock_reference_utc']
            del metadata['wall_clock_correlation']
            data = json.dumps(metadata).encode()
            found = True
        legacy.writestr(entry, data)
    assert found
astl.load_collection(sys.argv[2])
assert astl.get_system_info()['clock_correlation'] is None
with zipfile.ZipFile(sys.argv[1]) as source, zipfile.ZipFile(sys.argv[3], 'w') as legacy:
    for entry in source.infolist():
        if not entry.filename.endswith('platform_info.astl'):
            legacy.writestr(entry, source.read(entry.filename))
astl.load_collection(sys.argv[3])
assert astl.get_system_info()['clock_correlation'] is None
"""
    subprocess.run(
        [sys.executable, "-c", script, str(tmp_path / "clock.astl"),
         str(tmp_path / "legacy.astl"), str(tmp_path / "legacy_without_platform.astl")],
        check=True, capture_output=True, text=True, timeout=30,
    )
