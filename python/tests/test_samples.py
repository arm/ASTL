# SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

import pytest

import astl


def test_sample_retrieval_graceful_empty():
    """Sample retrieval should return a list (possibly empty) and not raise.

    If there are no targets or no counters/metrics, the test is skipped to
    avoid false failures in minimal environments.
    """
    targets = astl.get_targets()
    if not targets:
        pytest.skip("No targets available for sample retrieval test")

    t = targets[0]
    counters = astl.get_counters(t)
    metrics = astl.get_metrics(t)

    if not counters and not metrics:
        pytest.skip("No counters or metrics exposed by target")

    # Choose at most one to keep test quick
    if counters:
        c = counters[0]
        # Configuration may be a NO-OP if not implemented; should not raise
        params = astl.CollectionParameters(sampling_interval=10, mode=astl.CollectionMode.IMMEDIATE)
        astl.configure_counters_on_target(t, params, [c])
        astl.start_collection(t)
        samples = astl.get_counter_samples(t, c)
        assert isinstance(samples, list)
        # values (timestamp, value) pairs if present
        for pair in samples[:3]:
            assert isinstance(pair, tuple) and len(pair) == 2
        astl.stop_collection(t)
        metrics = astl.get_metrics(t)

    if metrics:
        m = metrics[0]
        params = astl.CollectionParameters(sampling_interval=10, mode=astl.CollectionMode.IMMEDIATE)
        try:
            astl.configure_metrics_on_target(t, params, [m])
            astl.start_collection(t)
            samples_m = astl.get_metric_samples(t, m)
            assert isinstance(samples_m, list)
            for pair in samples_m[:3]:
                assert isinstance(pair, tuple) and len(pair) == 2
        except astl.ASTLError as exc:
            if exc.code == astl.Status.METRIC_NOT_SUPPORTED_ON_TARGET:
                pytest.skip(f"Metric {m.name!r} is no longer supported on target {t.name!r}")
            raise
        finally:
            astl.stop_collection(t)
