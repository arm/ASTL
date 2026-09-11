# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

"""Public Python surface tests for metric event discovery."""

import astl


def test_metric_event_discovery_symbols_are_public() -> None:
    assert hasattr(astl, "get_metric_events_on_target")
    assert hasattr(astl, "EventProperties")


def test_metric_event_copies_python_strings() -> None:
    event = astl.EventProperties(value=3, name="ASTL_LIFECYCLE_CROP_END", description="End of crop window")
    assert event.value == 3
    assert event.name == "ASTL_LIFECYCLE_CROP_END"
    assert event.description == "End of crop window"
    assert "ASTL_LIFECYCLE_CROP_END" in repr(event)
