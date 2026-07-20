# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

import os
import faulthandler
import pytest

SKIP_IMPORT = os.environ.get("ASTL_SKIP_IMPORT") == "1"
if not SKIP_IMPORT:
    import astl  # type: ignore  # noqa: F401
else:
    print("[conftest] Skipping astl import due to ASTL_SKIP_IMPORT=1")


def pytest_sessionstart(session):  # noqa: ARG001
    faulthandler.enable()
    os.environ["ASTL_CONFIG_DIR"] = "tests/config"
    if SKIP_IMPORT:
        # Make explicit in logs that initialization is bypassed.
        print("[conftest] astl import bypassed (sdist integrity mode)")


def pytest_sessionfinish(session, exitstatus):  # noqa: ARG001
    # Shutdown API removed in this patch; placeholder for future cleanup hook
    pass
