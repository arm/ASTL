# SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

"""Tests for async streaming helpers (stream_counter / stream_metric).

Uses monkeypatching to avoid dependence on actual targets/counters.
We simulate a minimal subset of the API surface required by streaming helpers:
  * start_collection / stop_collection (no-op)
  * read_immediate (increments an internal counter)
  * get_counter_samples / get_metric_samples (return synthetic samples based on call count)

We then run a short async loop via stream_counter and stream_metric to ensure:
  * Correct number of iterations
  * PollResult objects contain expected synthetic timestamps
  * Timing logic yields without raising
"""
import asyncio
import astl
import astl.streaming as streaming


class _DummyEntity:
    def __init__(self, name: str):
        self.name = name


def _install_synthetic(monkeypatch):
    call_counter = {"n": 0}

    def read_immediate(target):  # noqa: ARG001
        call_counter["n"] += 1

    def get_counter_samples(target, counter):  # noqa: ARG001
        # two synthetic samples per call with increasing timestamp
        base = call_counter["n"] * 100
        return [(base + 1, 10), (base + 2, 11)]

    def get_metric_samples(target, metric):  # noqa: ARG001
        base = call_counter["n"] * 200
        return [(base + 5, 0.5), (base + 6, 0.75)]

    # no-op lifecycle
    def start_collection(target):  # noqa: ARG001
        return None

    def stop_collection(target):  # noqa: ARG001
        return None

    # Patch both the re-export points and the streaming module local references.
    monkeypatch.setattr(streaming, 'read_immediate', read_immediate)
    monkeypatch.setattr(streaming, 'get_counter_samples', get_counter_samples)
    monkeypatch.setattr(streaming, 'get_metric_samples', get_metric_samples)
    monkeypatch.setattr(streaming, 'start_collection', start_collection)
    monkeypatch.setattr(streaming, 'stop_collection', stop_collection)
    # For safety also patch top-level astl (not strictly needed now).
    monkeypatch.setattr(astl, 'read_immediate', read_immediate, raising=False)
    monkeypatch.setattr(astl, 'get_counter_samples', get_counter_samples, raising=False)
    monkeypatch.setattr(astl, 'get_metric_samples', get_metric_samples, raising=False)
    monkeypatch.setattr(astl, 'start_collection', start_collection, raising=False)
    monkeypatch.setattr(astl, 'stop_collection', stop_collection, raising=False)

    return call_counter


async def _collect_async_counter(counter, iterations: int):
    out = []
    async for pr in streaming.stream_counter(target=None, counter=counter, interval_sec=0.0001, iterations=iterations):
        out.append(pr)
    return out


async def _collect_async_metric(metric, iterations: int):
    out = []
    async for pr in streaming.stream_metric(target=None, metric=metric, interval_sec=0.0001, iterations=iterations):
        out.append(pr)
    return out


def test_async_stream_counter(monkeypatch):
    dummy = _DummyEntity("dummy_counter")
    _install_synthetic(monkeypatch)
    iterations = 3
    results = asyncio.run(_collect_async_counter(dummy, iterations))
    assert len(results) == iterations
    # Ensure timestamps of samples increase per iteration (synthetic pattern)
    ts_sequences = [r.samples[0][0] for r in results]
    assert ts_sequences == sorted(ts_sequences)


def test_async_stream_metric(monkeypatch):
    dummy = _DummyEntity("dummy_metric")
    _install_synthetic(monkeypatch)
    iterations = 2
    results = asyncio.run(_collect_async_metric(dummy, iterations))
    assert len(results) == iterations
    ts_sequences = [r.samples[0][0] for r in results]
    assert ts_sequences == sorted(ts_sequences)
