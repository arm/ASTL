#!/usr/bin/env python3

# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

"""Async streaming demo using asyncio and streaming helpers.

Demonstrates concurrent streaming of a counter and a metric (if available)
with a limited number of iterations.
"""
from __future__ import annotations
import asyncio
import astl  # Exposes async streaming helpers (stream_counter / stream_metric)


async def stream_counter_task(target, counter):
    # Consume the async counter stream for a bounded number of iterations
    async for res in astl.stream_counter(target, counter, interval_sec=0.25, iterations=5):
        latest = res.samples[-1] if res.samples else None
        print(f"[counter] {counter.name}: {len(res.samples)} samples (latest={latest})")


async def stream_metric_task(target, metric):
    # Consume the async metric stream
    async for res in astl.stream_metric(target, metric, interval_sec=0.25, iterations=5):
        latest = res.samples[-1] if res.samples else None
        print(f"[metric] {metric.name}: {len(res.samples)} samples (latest={latest})")


async def async_main():
    # Initialize and enumerate
    targets = astl.get_targets()
    if not targets:
        print("No targets detected; exiting.")
        return 0
    t = targets[0]
    counters = astl.get_counters(t)
    metrics = astl.get_metrics(t)
    if not counters and not metrics:
        print("Target has no counters or metrics; exiting.")
        return 0

    counter = counters[0] if counters else None
    metric = metrics[0] if metrics else None

    # Configure a basic immediate-mode collection for selected entities
    astl.configure_basic_collection(t, [counter] if counter else [], [metric] if metric else [])

    tasks = []  # Launch tasks concurrently (only those available)
    if counter:
        tasks.append(asyncio.create_task(stream_counter_task(t, counter)))
    if metric:
        tasks.append(asyncio.create_task(stream_metric_task(t, metric)))
    await asyncio.gather(*tasks)
    return 0


def main():
    return asyncio.run(async_main())


if __name__ == "__main__":
    raise SystemExit(main())
