import importlib, sys, types
import astl
from astl import Status, map_status_to_exception, InitializationError, InternalError, NotImplementedErrorASTL


def test_multiple_reloads_preserve_not_initialized_mapping():
    # Baseline
    assert map_status_to_exception(Status.NOT_INITIALIZED) is InitializationError
    for _ in range(3):
        sys.modules.pop('astl.exceptions', None)
        importlib.import_module('astl.exceptions')
        from astl import map_status_to_exception as m
        assert m(Status.NOT_INITIALIZED) is InitializationError


def test_mapping_after_partial_stub_injection(monkeypatch):
    # Simulate early import capturing a stub Status lacking NOT_INITIALIZED
    # by reloading module after monkeypatching astl.exceptions.Status temporarily
    import astl.exceptions as ex
    class FakeStatus:  # missing NOT_INITIALIZED intentionally
        NOT_IMPLEMENTED = getattr(Status, 'NOT_IMPLEMENTED', 7)
    monkeypatch.setattr(ex, 'Status', FakeStatus, raising=True)
    # Force map call while stub active (should return None for NOT_INITIALIZED, but not crash)
    # Accept either None (if stub prevents recognition) or the correct InitializationError mapping.
    result = ex.map_status_to_exception(getattr(Status, 'NOT_INITIALIZED'))
    # Identity should normally hold; if import system gave us a reloaded class, fall back to name check.
    assert result is None or result is InitializationError or getattr(result, '__name__', '') == 'InitializationError'
    # Drop module and reload (should self-heal to real Status and map properly)
    sys.modules.pop('astl.exceptions', None)
    importlib.import_module('astl.exceptions')
    from astl import map_status_to_exception as m
    assert m(Status.NOT_INITIALIZED) is InitializationError


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
