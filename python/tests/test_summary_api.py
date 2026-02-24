"""Tests for astl.get_metric_statistics / MetricStatistics."""
import pytest

import astl


# ---------------------------------------------------------------------------
# Public API surface
# ---------------------------------------------------------------------------

def test_metric_statistics_exported():
    """MetricStatistics and get_metric_statistics must be importable from astl."""
    assert hasattr(astl, "get_metric_statistics")
    assert hasattr(astl, "MetricStatistics")


def test_metric_statistics_attributes():
    """MetricStatistics instances expose count, min, max, avg."""
    s = astl.MetricStatistics(count=3, min=10, max=30, avg=20.0)
    assert s.count == 3
    assert s.min == 10
    assert s.max == 30
    assert s.avg == pytest.approx(20.0)


def test_metric_statistics_zero_count():
    """count=0 means no samples; min/max/avg should be None."""
    s = astl.MetricStatistics(count=0, min=None, max=None, avg=None)
    assert s.count == 0
    assert s.min is None
    assert s.max is None
    assert s.avg is None


def test_metric_statistics_repr():
    """__repr__ should include count, min, max, avg."""
    s = astl.MetricStatistics(count=2, min=5, max=15, avg=10.0)
    r = repr(s)
    assert "count=2" in r
    assert "min=5" in r
    assert "max=15" in r
    assert "avg=10.0" in r


# ---------------------------------------------------------------------------
# Integration test (skipped when no live targets are available)
# ---------------------------------------------------------------------------

def test_get_metric_statistics_live():
    """End-to-end: collect samples then call get_metric_statistics.

    Skipped when no targets / arithmetic metrics are available (CI without
    hardware) or the metric type is not supported by the summarizer.
    """
    targets = astl.get_targets()
    if not targets:
        pytest.skip("No targets available")

    target = targets[0]
    metrics = astl.get_metrics(target)

    # Filter to arithmetic (non-bool, non-string) metrics
    arithmetic_types = {
        astl.ValueType.UINT8,
        astl.ValueType.UINT16,
        astl.ValueType.UINT32,
        astl.ValueType.UINT64,
        astl.ValueType.FLOAT32,
        astl.ValueType.FLOAT64,
    }
    arithmetic_metrics = [m for m in metrics if m.value_type in arithmetic_types]
    if not arithmetic_metrics:
        pytest.skip("No arithmetic metrics available on first target")

    metric = arithmetic_metrics[0]
    params = astl.CollectionParameters(sampling_interval=0, mode=astl.CollectionMode.IMMEDIATE)
    astl.configure_metrics_on_target(target, params, [metric])
    astl.start_collection(target)
    astl.read_immediate(target)
    astl.stop_collection(target)

    summary = astl.get_metric_statistics(target, metric)

    assert isinstance(summary, astl.MetricStatistics)
    assert isinstance(summary.count, int)
    assert summary.count >= 0

    if summary.count > 0:
        assert summary.min is not None
        assert summary.max is not None
        assert summary.avg is not None
        # min <= avg <= max (numerically)
        assert float(summary.min) <= float(summary.avg) + 1e-9
        assert float(summary.avg) <= float(summary.max) + 1e-9
