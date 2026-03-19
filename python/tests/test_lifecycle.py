# SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

import pytest

import astl


def test_lifecycle_no_exception():
    """Lifecycle wrappers should never raise even if NOT_IMPLEMENTED in C layer.

    We call them both without a target (NULL -> global) and, if available, with the
    first detected target. The test passes if no exception is raised.
    """

    # Without explicit target
    astl.start_collection()
    astl.start_collection_paused()
    astl.pause_collection()
    astl.resume_collection()
    astl.read_immediate()
    astl.stop_collection()

    targets = astl.get_targets()
    if targets:
        t = targets[0]
        astl.start_collection(t)
        astl.start_collection_paused(t)
        astl.pause_collection(t)
        astl.resume_collection(t)
        astl.read_immediate(t)
        astl.stop_collection(t)
