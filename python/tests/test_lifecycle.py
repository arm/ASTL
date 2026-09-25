# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

import pytest

import astl


def test_lifecycle_no_exception():
    """Lifecycle wrappers should tolerate recoverable configuration-state statuses.

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


def test_stopped_collection_requires_reconfiguration():
    """Stopped collections report recoverable state errors, not success."""
    for target in astl.get_targets():
        counters = astl.get_counters(target)
        if counters:
            break
    else:
        pytest.skip("No target with counters available")

    params = astl.CollectionParameters(sampling_interval=10, mode=astl.CollectionMode.IMMEDIATE)
    astl.configure_counters_on_target(target, params, [counters[0]])
    astl.start_collection(target)
    astl.stop_collection(target)

    for action in (astl.stop_collection, astl.pause_collection, astl.resume_collection):
        with pytest.raises(astl.ASTLError) as exc:
            action(target)
        assert exc.value.code == astl.Status.COLLECTION_ALREADY_STOPPED

    with pytest.raises(astl.ASTLError) as exc:
        astl.start_collection(target)
    assert exc.value.code == astl.Status.INVALID_STATE_TRANSITION

    astl.configure_counters_on_target(target, params, [counters[0]])
    astl.start_collection(target)
    astl.stop_collection(target)
