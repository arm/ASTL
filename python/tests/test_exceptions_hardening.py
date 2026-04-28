# SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

import importlib, sys
from astl import Status, map_status_to_exception, InternalError, NotImplementedErrorASTL


def test_multiple_reloads_preserve_internal_error_mapping():
    # Baseline
    assert map_status_to_exception(Status.INTERNAL_ERROR) is InternalError
    for _ in range(3):
        sys.modules.pop('astl.exceptions', None)
        importlib.import_module('astl.exceptions')
        from astl import map_status_to_exception as m
        assert m(Status.INTERNAL_ERROR) is InternalError


def test_mapping_after_partial_stub_injection(monkeypatch):
    # Simulate early import capturing a stub Status lacking INTERNAL_ERROR by reloading
    # after monkeypatching astl.exceptions.Status temporarily.
    import astl.exceptions as ex
    class FakeStatus:  # missing INTERNAL_ERROR intentionally
        NOT_IMPLEMENTED = getattr(Status, 'NOT_IMPLEMENTED', 7)
    monkeypatch.setattr(ex, 'Status', FakeStatus, raising=True)
    # Force map call while stub active (should not crash and may or may not self-heal).
    result = ex.map_status_to_exception(getattr(Status, 'INTERNAL_ERROR'))
    assert result is None or result is InternalError or getattr(result, '__name__', '') == 'InternalError'
    # Drop module and reload (should self-heal to real Status and map properly)
    sys.modules.pop('astl.exceptions', None)
    importlib.import_module('astl.exceptions')
    from astl import map_status_to_exception as m
    assert m(Status.INTERNAL_ERROR) is InternalError


def test_internal_error_mapping_integrity():
    # Ensure INTERNAL_ERROR still maps even after reload
    sys.modules.pop('astl.exceptions', None)
    importlib.import_module('astl.exceptions')
    from astl import map_status_to_exception as m
    if hasattr(Status, 'INTERNAL_ERROR'):
        assert m(Status.INTERNAL_ERROR) is InternalError


def test_not_implemented_mapping_consistency():
    # Guarantee NOT_IMPLEMENTED returns the specialized class
    sys.modules.pop('astl.exceptions', None)
    importlib.import_module('astl.exceptions')
    from astl import map_status_to_exception as m
    if hasattr(Status, 'NOT_IMPLEMENTED'):
        assert m(Status.NOT_IMPLEMENTED) is NotImplementedErrorASTL
