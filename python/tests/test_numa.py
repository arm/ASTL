# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

import os
from pathlib import Path
import subprocess
import sys

import pytest


def test_numa_discovery_and_percentage_collection(tmp_path):
    if sys.platform != "linux":
        pytest.skip("NUMA discovery is Linux-only")
    env = dict(os.environ)
    env["ASTL_CONFIG_DIR"] = str(Path(__file__).resolve().parents[2] / "config")
    env["ASTL_COLLECTORS"] = "numa"
    env["ASTL_NUMA_ROOT"] = str(tmp_path)
    # A fresh process isolates the binding's global ASTL session from other tests.
    result = subprocess.run([sys.executable, "-c", """
import os
from pathlib import Path
import astl
root = Path(os.environ["ASTL_NUMA_ROOT"])
def write(node, local, remote):
    path = root / node
    path.mkdir(exist_ok=True)
    (path / "numastat").write_text(
        f"numa_hit {local}\\nnuma_miss {remote}\\ninterleave_hit 0\\n"
        f"local_node {local}\\nother_node {remote}\\n")
write("node0", 100, 100)
write("node2", 100, 100)
targets = astl.get_targets()
assert len(targets) == 1 and targets[0].name == "numa"
target = targets[0]
counters = astl.get_counters(target)
assert len(counters) == 9
assert all(counter.counter_type == astl.CounterType.VALUE for counter in counters)
assert all(counter.units == astl.Units.COUNT for counter in counters)
metrics = {metric.name: metric for metric in astl.get_metrics(target)}
assert len(metrics) == 9
assert {"numa_node0_allocations", "numa_node2_allocations"} <= metrics.keys()
metric = metrics["Linux_numa_local_percent"]
assert metric.units == astl.Units.PERCENT
assert metric.value_type == astl.ValueType.FLOAT64
params = astl.CollectionParameters(sampling_interval=10, mode=astl.CollectionMode.IMMEDIATE, flags=astl.CollectionParameterFlags.NONE)
counter = next(counter for counter in counters if counter.name == "Linux_numa_local_percent")
astl.configure_counters_on_target(target, params, [counter])
astl.start_collection(target)
astl.read_immediate(target)
write("node0", 175, 125)
astl.read_immediate(target)
astl.stop_collection(target)
samples = astl.get_counter_samples(target, counter)
assert len(samples) == 1 and samples[0][1] == 7500, samples
metric = next(metric for metric in astl.get_metrics(target) if metric.name == "Linux_numa_local_percent")
astl.configure_metrics_on_target(target, params, [metric])
astl.start_collection(target)
astl.read_immediate(target)
write("node0", 250, 150)
astl.read_immediate(target)
astl.stop_collection(target)
samples = astl.get_metric_samples(target, metric)
assert len(samples) == 1 and samples[0][1] == 75.0, samples
"""], env=env, capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, result.stdout + result.stderr
