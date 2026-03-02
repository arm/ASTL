# SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

import pytest

import astl

def test_get_targets():
    targets = astl.get_targets()
    assert isinstance(targets, list)
    # Targets may be empty; just ensure list semantics
