# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

import importlib
import pathlib
import pytest

EXPECTED_HEADERS = {
    "astl.h",
    "astl_errors.h",
    "astl_telemetry.h",
    "astl_utils.h",
    "astl_version.h",
}


@pytest.mark.parametrize("filename", sorted(EXPECTED_HEADERS))
def test_vendored_header_present(filename):
    """Each expected vendored public header should be present in the installed package.

    This guards against accidental omissions in sdist/wheel builds when
    vendoring logic changes.
    """
    pkg = importlib.import_module("astl")
    base = pathlib.Path(pkg.__file__).parent
    header = base / "include" / "astl" / filename
    assert header.is_file(), f"Expected header not found at {header}"
