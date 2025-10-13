import pytest

import astl

def test_not_initialized_error_message():
    with pytest.raises(Exception) as excinfo:
        astl.get_targets()
    assert 'initialize()' in str(excinfo.value)

def test_initialize_and_get_targets():
    astl.initialize(None)
    targets = astl.get_targets()
    assert isinstance(targets, list)
    # Targets may be empty; just ensure list semantics
