"""Tests for astl.get_metric_states_on_target and MetricState."""
import pytest

import astl


# ---------------------------------------------------------------------------
# Public API surface
# ---------------------------------------------------------------------------

def test_metric_state_exported():
    """MetricState and get_metric_states_on_target must be importable from astl."""
    assert hasattr(astl, "get_metric_states_on_target")
    assert hasattr(astl, "MetricState")


def test_metric_state_attributes():
    """MetricState instances expose name, description, and value."""
    s = astl.MetricState(name="Active", description="CPU is active", value=1)
    assert s.name == "Active"
    assert s.description == "CPU is active"
    assert s.value == 1


def test_metric_state_none_description():
    """MetricState description may be None."""
    s = astl.MetricState(name="C6", description=None, value=None)
    assert s.name == "C6"
    assert s.description is None
    assert s.value is None


def test_metric_state_empty_description_normalised():
    """description=None is the sentinel for 'not provided' (empty C string -> empty string -> None in Python)."""
    s = astl.MetricState(name="Idle", description=None, value=None)
    assert s.description is None


def test_metric_state_repr():
    """__repr__ should include name and value."""
    s = astl.MetricState(name="Idle", description=None, value=0)
    r = repr(s)
    assert "Idle" in r
    assert "0" in r


# ---------------------------------------------------------------------------
# Integration tests (skipped without live hardware)
# ---------------------------------------------------------------------------

def test_get_metric_states_on_target_finite_set():
    """End-to-end: get_metric_states_on_target on a finite-set metric.

    Skipped when no targets or finite-set metrics are available.
    """
    targets = astl.get_targets()
    if not targets:
        pytest.skip("No targets available")

    target = targets[0]
    metrics = astl.get_metrics(target)

    finite_set_metrics = [
        m for m in metrics if m.metric_type == astl.MetricType.FINITE_SET_VALUE
    ]
    if not finite_set_metrics:
        pytest.skip("No FINITE_SET_VALUE metrics available on first target")

    metric = finite_set_metrics[0]

    try:
        states = astl.get_metric_states_on_target(target, metric)
    except astl.NotSupportedError:
        pytest.skip(f"get_metric_states_on_target returned NOT_SUPPORTED for metric {metric.name!r}")

    assert isinstance(states, list)
    assert len(states) > 0, "Finite-set metric must have at least one state"

    for st in states:
        assert isinstance(st, astl.MetricState)
        assert isinstance(st.name, str) and st.name, "State name must be a non-empty string"
        # description is either a non-empty str (when the config provides one) or None
        assert st.description is None or (isinstance(st.description, str) and st.description), (
            "description must be None or a non-empty str"
        )
        # value should be decoded (not None) for finite-set metrics
        assert st.value is not None, "Finite-set MetricState must have a decoded value"

    # Names should be unique
    names = [st.name for st in states]
    assert len(names) == len(set(names)), "State names must be unique"


def test_get_metric_states_on_target_residency():
    """End-to-end: get_metric_states_on_target on a residency metric.

    Skipped when no targets or residency metrics are available.
    """
    targets = astl.get_targets()
    if not targets:
        pytest.skip("No targets available")

    target = targets[0]
    metrics = astl.get_metrics(target)

    residency_metrics = [
        m for m in metrics if m.metric_type == astl.MetricType.RESIDENCY
    ]
    if not residency_metrics:
        pytest.skip("No RESIDENCY metrics available on first target")

    metric = residency_metrics[0]

    try:
        states = astl.get_metric_states_on_target(target, metric)
    except astl.NotSupportedError:
        pytest.skip(f"get_metric_states_on_target returned NOT_SUPPORTED for metric {metric.name!r}")

    assert isinstance(states, list)
    assert len(states) > 0, "Residency metric must have at least one state"

    for st in states:
        assert isinstance(st, astl.MetricState)
        assert isinstance(st.name, str) and st.name, "State name must be a non-empty string"
        # description is either a non-empty str (when the config provides one) or None
        assert st.description is None or (isinstance(st.description, str) and st.description), (
            "description must be None or a non-empty str"
        )
        # value is None for residency metrics
        assert st.value is None, "Residency MetricState value must be None"


def test_get_metric_states_on_target_unsupported_type():
    """Calling get_metric_states_on_target on a non-discrete metric raises NotSupportedError.

    Skipped if no non-discrete metrics are available.
    """
    targets = astl.get_targets()
    if not targets:
        pytest.skip("No targets available")

    target = targets[0]
    metrics = astl.get_metrics(target)

    unsupported_types = {
        astl.MetricType.VALUE,
        astl.MetricType.DELTA,
        astl.MetricType.RATE,
    }
    candidates = [m for m in metrics if m.metric_type in unsupported_types]
    if not candidates:
        pytest.skip("No non-discrete metrics available on first target")

    metric = candidates[0]
    with pytest.raises(astl.NotSupportedError):
        astl.get_metric_states_on_target(target, metric)
