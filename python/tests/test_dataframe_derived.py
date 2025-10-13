import pytest

from astl import to_dataframe, deltas, rates


def test_deltas_basic():
    samples = [(1000, 10), (2000, 25), (3000, 40)]
    d = deltas(samples)
    # Expect two delta points (len-1)
    assert len(d) == 2
    # First delta: 25-10 = 15
    assert d[0][1] == 15
    # Second delta: 40-25 = 15
    assert d[1][1] == 15


def test_rates_basic():
    samples = [(1000, 10), (2000, 25), (4000, 65)]  # uneven spacing
    r = rates(samples, time_scale=1000)  # timestamps are ms, scale to per-second
    assert len(r) == 2
    # First rate: (25-10)/(2000-1000) * 1000 = 15 per second
    assert r[0][1] == 15
    # Second rate: (65-25)/(4000-2000) * 1000 = 40/2000*1000 = 20 per second
    assert r[1][1] == 20


def test_rates_short_sequence():
    # fewer than 2 points -> empty
    assert rates([(1000, 5)]) == []


def test_to_dataframe_no_pandas(monkeypatch):
    """Force the ImportError path regardless of whether pandas is installed.

    Deleting pandas from sys.modules is insufficient when pandas is actually
    installed, because a subsequent import will succeed. We monkeypatch
    builtins.__import__ to raise ImportError specifically for 'pandas'.
    """
    import builtins
    import sys

    original_import = builtins.__import__

    def _fake_import(name, *args, **kwargs):  # pragma: no cover - small shim
        if name == "pandas":
            raise ImportError("Forced missing pandas for test")
        return original_import(name, *args, **kwargs)

    monkeypatch.setattr(builtins, "__import__", _fake_import)
    # Also remove any preloaded module reference so code path tries to import
    sys.modules.pop("pandas", None)

    data = {"a": [(1, 10), (2, 12)]}
    with pytest.raises(ImportError):
        to_dataframe(data, silent=False)
    # silent True returns None
    assert to_dataframe(data, silent=True) is None
