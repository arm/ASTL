import sys
import types
import pytest

from astl import to_dataframe


class _Boom(Exception):
    pass


def test_to_dataframe_does_not_swallow_non_import_errors(monkeypatch):
    """If pandas imports successfully but later logic errors occur, they should propagate.

    We simulate a minimal pandas module exposing just DataFrame and rely on our helper
    constructing rows. The injected DataFrame raises a sentinel exception to confirm
    it isn't wrapped or suppressed by to_dataframe's ImportError handling.
    """
    fake_pandas = types.ModuleType("pandas")

    def _fake_dataframe(rows, columns=None):  # signature similar enough
        raise _Boom("dataframe construction failed")

    fake_pandas.DataFrame = _fake_dataframe  # type: ignore[attr-defined]

    # Inject into sys.modules
    monkeypatch.setitem(sys.modules, "pandas", fake_pandas)

    with pytest.raises(_Boom):
        to_dataframe({"x": [(1, 2)]})

    # Silent=True should still propagate non-import errors (documented contract only covers ImportError)
    with pytest.raises(_Boom):
        to_dataframe({"x": [(1, 2)]}, silent=True)
