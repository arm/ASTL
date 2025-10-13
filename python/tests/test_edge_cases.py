"""Edge case tests for derived rates() and diagnostics initialization path."""
import astl


def test_rates_zero_delta_time():
    # Two samples with identical timestamps should yield an 'inf' rate
    samples = [(100, 10), (100, 30)]
    r = astl.rates(samples, time_scale=1.0)
    assert len(r) == 1
    ts, val = r[0]
    assert ts == 100
    assert val == float('inf')


def test_rates_non_numeric_value():
    samples = [(1, 10), (2, 'x')]  # second value non-numeric -> nan
    r = astl.rates(samples)
    assert len(r) == 1
    assert r[0][1] != r[0][1]  # NaN check (NaN != NaN)


def test_diagnostics_initialize_if_needed(monkeypatch):
    import importlib
    diag_mod = importlib.import_module('astl.diagnostics')

    calls = {"init": 0}

    def fake_initialize(arg=None):  # noqa: ARG001
        calls["init"] += 1

    def fake_version():
        return (0, 0, 0, "0.0.test")

    def fake_get_targets():
        return [object(), object()]

    # Patch inside diagnostics module so its already-imported symbols are replaced
    monkeypatch.setattr(diag_mod, 'version', fake_version, raising=False)
    monkeypatch.setattr(diag_mod, 'get_targets', fake_get_targets, raising=False)
    # Patch initialize via the local import path used inside diagnostics()
    monkeypatch.setattr(astl, 'initialize', fake_initialize, raising=False)

    d = diag_mod.diagnostics(initialize_if_needed=True)
    assert d.target_count == 2
    assert d.astl_version == "0.0.test"
    assert calls["init"] == 1
