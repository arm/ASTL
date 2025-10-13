import pytest

from astl import (
    initialize,
    InitializationError,
    InvalidArgumentError,
    BadArgumentError,
    NotSupportedError,
    InternalError,
    map_status_to_exception,
    Status,
)


def test_map_status_to_exception_known_codes():
    assert map_status_to_exception(Status.NOT_INITIALIZED) is InitializationError
    # BAD_ARGUMENT should map now
    if hasattr(Status, 'BAD_ARGUMENT'):
        assert map_status_to_exception(Status.BAD_ARGUMENT) in (BadArgumentError, InvalidArgumentError)
    # INVALID_ARGUMENT may exist; if so ensure mapping
    if hasattr(Status, 'INVALID_ARGUMENT'):
        assert map_status_to_exception(Status.INVALID_ARGUMENT) is InvalidArgumentError
    if hasattr(Status, 'NOT_SUPPORTED'):
        assert map_status_to_exception(Status.NOT_SUPPORTED) is NotSupportedError
    if hasattr(Status, 'INTERNAL_ERROR'):
        assert map_status_to_exception(Status.INTERNAL_ERROR) is InternalError

def test_initialization_error_message():
    # Simulate raising the mapped exception directly to validate message augmentation
    with pytest.raises(InitializationError) as ei:
        raise InitializationError(Status.NOT_INITIALIZED)
    assert 'initialize' in str(ei.value).lower()

def test_initialize_then_no_mapping_side_effect():
    # After initialize mapping still returns same class; this just ensures calling initialize doesn't break map
    initialize()
    assert map_status_to_exception(Status.NOT_INITIALIZED) is InitializationError
