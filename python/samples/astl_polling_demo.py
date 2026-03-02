#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

"""Periodic polling demo using synchronous helpers.

Shows how to:
  * Initialize ASTL
  * Discover first target and a counter/metric
  * Configure basic collection
  * Poll a counter/metric periodically for a few iterations

Graceful behavior when no targets / counters / metrics exist.
"""
from __future__ import annotations
import astl  # Provides both low-level functions and higher-level streaming helpers

def _select_target():  # -> astl.Target | None (leave annotated loosely for runtime simplicity)
    """Return the first available target or None if none exist."""
    targets = astl.get_targets()
    if not targets:
        print("No targets available; exiting.")
        return None
    return targets[0]


def _choose_first_resources(target):
    """Return a tuple (counter_or_None, metric_or_None) for the target."""
    counters = astl.get_counters(target)
    metrics = astl.get_metrics(target)
    if not counters and not metrics:
        print("Target exposes no counters or metrics; exiting.")
        return None, None
    return (counters[0] if counters else None, metrics[0] if metrics else None)


def _configure_collection(target, counter, metric) -> None:
    """Configure minimal immediate-mode collection for any discovered objects."""
    astl.configure_basic_collection(
        target,
        [counter] if counter else [],
        [metric] if metric else [],
    )


def _poll(counter, metric, target, iterations: int = 5, interval: float = 0.2) -> None:
    """Poll any available counter/metric periodically and print sample counts."""
    print(f"Polling for {iterations} iterations every {interval}s...")
    if counter:
        for res in astl.poll_counter_periodic(target, counter, interval, iterations):
            latest = res.samples[-1] if res.samples else None
            print(f"Counter {counter.name}: {len(res.samples)} samples (latest={latest})")
    if metric:
        for res in astl.poll_metric_periodic(target, metric, interval, iterations):
            latest = res.samples[-1] if res.samples else None
            print(f"Metric {metric.name}: {len(res.samples)} samples (latest={latest})")


def main() -> int:
    target = _select_target()
    if not target:
        return 0
    counter, metric = _choose_first_resources(target)
    if not counter and not metric:
        return 0
    _configure_collection(target, counter, metric)
    _poll(counter, metric, target)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
