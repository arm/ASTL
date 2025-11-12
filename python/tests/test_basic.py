import pytest

import astl

def test_get_targets():
    targets = astl.get_targets()
    assert isinstance(targets, list)
    # Targets may be empty; just ensure list semantics
