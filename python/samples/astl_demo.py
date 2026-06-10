#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

"""ASTL Python bindings demonstration.

Demonstrates:
    * Initialization & version
    * Target, counter, metric, metric-group enumeration
    * Configuring a simple collection (best-effort: target-specific unsupported items tolerated)
    * Starting/stopping collection lifecycle
    * Retrieving (possibly empty) counter / metric samples

Prerequisites:
    1. Build native library (e.g. ./build_debug.sh or CMake preset) so libastl-<MAJOR>.so exists.
    2. Install the Python package in editable mode:
                 python -m pip install -e python

The setup script copies the built shared library into the astl package and
sets an rpath ($ORIGIN) so you should NOT need to set LD_LIBRARY_PATH.
"""
from __future__ import annotations
import os
import sys
import tempfile
from typing import Sequence

try:
    import astl
except ImportError as e:
    print("Failed to import astl package. Did you run 'python -m pip install -e python'?", file=sys.stderr)
    raise


def _print_header(title: str):
    print("\n" + title)
    print("-" * len(title))


def _print_collection_entities(title: str, items: Sequence, formatter):
    _print_header(title)
    if not items:
        print("(none)")
        return
    for obj in items[:10]:  # limit output
        print(formatter(obj))


def _enumerate_entities(target):
    counters = astl.get_counters(target)
    metrics = astl.get_metrics(target)
    groups = astl.get_metric_groups_on_target(target)
    _print_collection_entities(
        "Counters",
        counters,
        lambda c: f"{c.name} (min_interval={c.min_sampling_interval}us type={c.counter_type} value_type={c.value_type})",
    )
    _print_collection_entities(
        "Metrics",
        metrics,
        lambda m: f"{m.name} (min_interval={m.min_sampling_interval}us type={m.metric_type} value_type={m.value_type})",
    )
    _print_collection_entities(
        "Metric Groups",
        groups,
        lambda g: g.name,
    )
    return counters, metrics, groups


def _configure_and_collect(target, counters, metrics):
    to_collect_counters = counters[:1]
    to_collect_metrics = metrics[:1]
    if not (to_collect_counters or to_collect_metrics):
        _print_header("Configure & Collect")
        print("No counters or metrics available to configure")
        return

    _print_header("Configure & Collect (best-effort)")
    min_intervals = [c.min_sampling_interval for c in to_collect_counters] + [m.min_sampling_interval for m in to_collect_metrics]
    sampling_interval = max(min_intervals + [0])
    params = astl.CollectionParameters(
        sampling_interval=sampling_interval,
        mode=astl.CollectionMode.IMMEDIATE,
    )
    if to_collect_counters:
        astl.configure_counters_on_target(target, params, to_collect_counters)
    if to_collect_metrics:
        astl.configure_metrics_on_target(target, params, to_collect_metrics)
    astl.start_collection_paused(target)
    astl.resume_collection(target)
    astl.read_immediate(target)
    astl.stop_collection(target)

    _print_header("Samples")
    if to_collect_counters:
        c = to_collect_counters[0]
        samples_c = astl.get_counter_samples(target, c)
        print(f"Counter {c.name} samples: {samples_c[:5]}")
    if to_collect_metrics:
        m = to_collect_metrics[0]
        samples_m = astl.get_metric_samples(target, m)
        print(f"Metric {m.name} samples: {samples_m[:5]}")


def _save_load_roundtrip():
    """Demonstrate Python save/load wrappers for ASTL session archives."""
    _print_header("Save / Load Session (.astl)")
    session_path = os.path.join(tempfile.gettempdir(), "astl_python_demo_session.astl")
    print(f"Saving session to: {session_path}")
    astl.save_collection(session_path)
    print("Save complete")

    print(f"Loading session from: {session_path}")
    astl.load_collection(session_path)
    print("Load complete")


def main():
    config = os.environ.get("ASTL_CONFIG")
    astl.initialize(config)
    ver = astl.version()
    print(f"ASTL version: {ver[-1]}")

    _print_header("Targets")
    targets = astl.get_targets()
    if not targets:
        print("(none detected)")
        return 0
    for idx, t in enumerate(targets):
        print(f"[{idx}] {t.name} - {t.description}")

    target = targets[0]
    counters, metrics, _ = _enumerate_entities(target)
    _configure_and_collect(target, counters, metrics)
    try:
        _save_load_roundtrip()
    except astl.ASTLError as e:
        # Keep this sample resilient across environments where save/load may not be available.
        print(f"Save/load demo skipped due to ASTL error: {e}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
