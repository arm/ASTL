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
