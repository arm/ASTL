# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

"""Tests for astl.get_metric_statistics_on_target / MetricStatistics and
astl.get_metric_discrete_histogram_on_target / DiscreteHistogramBin."""
import pytest

import astl


# ---------------------------------------------------------------------------
# Public API surface
# ---------------------------------------------------------------------------

def test_metric_statistics_exported():
    """MetricStatistics and get_metric_statistics_on_target must be importable from astl."""
    assert hasattr(astl, "get_metric_statistics_on_target")
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

def test_get_metric_statistics_on_target_live():
    """End-to-end: collect samples then call get_metric_statistics_on_target.

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

    summary = astl.get_metric_statistics_on_target(target, metric)

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


# ---------------------------------------------------------------------------
# Discrete histogram
# ---------------------------------------------------------------------------

def test_get_metric_discrete_histogram_on_target_live():
    """End-to-end: collect samples then call get_metric_discrete_histogram_on_target.

    Skipped when no targets / discrete-friendly metrics are available (CI
    without hardware) or the metric type is not supported by the summarizer.
    """
    targets = astl.get_targets()
    if not targets:
        pytest.skip("No targets available")

    target = targets[0]
    metrics = astl.get_metrics(target)

    integer_types = {
        astl.ValueType.UINT8,
        astl.ValueType.UINT16,
        astl.ValueType.UINT32,
        astl.ValueType.UINT64,
    }
    candidates = [m for m in metrics if m.value_type in integer_types]
    if not candidates:
        pytest.skip("No integer metrics available on first target")

    metric = candidates[0]
    params = astl.CollectionParameters(sampling_interval=0, mode=astl.CollectionMode.IMMEDIATE)
    astl.configure_metrics_on_target(target, params, [metric])
    astl.start_collection(target)
    astl.read_immediate(target)
    astl.stop_collection(target)

    try:
        bins = astl.get_metric_discrete_histogram_on_target(target, metric)
    except astl.NotSupportedError:
        pytest.skip(f"Metric type {metric.value_type} not supported by discrete histogram summarizer")

    assert isinstance(bins, list)
    for b in bins:
        assert isinstance(b, astl.DiscreteHistogramBin)
        assert isinstance(b.count, int)
        assert b.count > 0
        assert b.value is not None

    # Each unique value should appear exactly once across all bins
    values = [b.value for b in bins]
    assert len(values) == len(set(values)), "Histogram bins must have unique values"

    # Total sample count across bins must be >= number of bins
    total_samples = sum(b.count for b in bins)
    assert total_samples >= len(bins)
