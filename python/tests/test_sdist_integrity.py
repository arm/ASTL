# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

import tarfile
import json
import subprocess
import sys
import pathlib

import pytest

REQUIRED_HEADER_SUBPATHS = {
    'astl/include/astl/astl.h',
    'astl/include/astl/astl_errors.h',
    'astl/include/astl/astl_telemetry.h',
    # Intentionally exclude astl_test_hooks.h (internal/testing only)
    'astl/include/astl/astl_utils.h',
    'astl/include/astl/astl_version.h',
}

REQUIRED_CYTHON_SOURCES = {
    'astl/_core.pyx',
    'astl/_core.pxd',
}

@pytest.mark.slow
def test_sdist_includes_headers_and_cython_sources(tmp_path):
    """Build an sdist and assert all required headers & Cython sources are present.

    This ensures packaging changes (MANIFEST, vendoring) remain intact.
    """
    project_root = pathlib.Path(__file__).resolve().parents[2]
    dist_dir = tmp_path / 'dist'
    dist_dir.mkdir()

    # Ensure vendored headers are refreshed before building sdist.
    subprocess.run([
        'bash', 'scripts/vendor_headers.sh', '--quiet'
    ], check=True, cwd=project_root / 'python')

    # Build sdist (only) into temporary directory.
    subprocess.run([
        sys.executable,
        '-m', 'build', '--sdist', '--outdir', str(dist_dir)
    ], check=True, cwd=project_root / 'python')

    sdists = list(dist_dir.glob('astl-*.tar.gz'))
    assert sdists, 'No sdist produced'
    sdist_path = sdists[0]

    missing = []
    with tarfile.open(sdist_path, 'r:gz') as tf:
        members = {m.name.split('/', 1)[1] for m in tf.getmembers() if '/' in m.name}
        for rel in REQUIRED_HEADER_SUBPATHS | REQUIRED_CYTHON_SOURCES:
            if rel not in members:
                missing.append(rel)
    assert not missing, f'Missing from sdist: {missing}'
