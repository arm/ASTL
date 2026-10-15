# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

import astl
from astl._core import Counter


def test_version_struct():
    maj, min_, mic, s = astl.version()
    assert isinstance(maj, int) and isinstance(min_, int) and isinstance(mic, int)
    assert s.count('.') >= 1


def test_empty_enumerations_do_not_error():
    targets = astl.get_targets()
    assert isinstance(targets, list)
    if targets:
        t = targets[0]
        counters = astl.get_counters(t)
        metrics = astl.get_metrics(t)
        groups = astl.get_metric_groups_on_target(t)
        all_groups = astl.get_metric_groups()
        # Types and list semantics
        assert isinstance(counters, list)
        assert isinstance(metrics, list)
        assert isinstance(groups, list)
        assert isinstance(all_groups, list)


def test_counter_preserves_qualified_scmi_name_and_type():
    gauge = Counter("SOC.0.TEMP_PRESENT", "SoC temperature", 1, 0, 0, 0, astl.CounterType.VALUE, "")
    count = Counter("SOC.0.ENERGY_COUNTER", "SoC energy", 2, 0, 0, 0, astl.CounterType.COUNT, "")

    assert gauge.name == "SOC.0.TEMP_PRESENT"
    assert astl.CounterType(gauge.counter_type) is astl.CounterType.VALUE
    assert count.name == "SOC.0.ENERGY_COUNTER"
    assert astl.CounterType(count.counter_type) is astl.CounterType.COUNT


def test_system_utilization_metrics_are_discoverable():
    import os
    from pathlib import Path
    import subprocess
    import sys
    import pytest

    if sys.platform != "linux":
        pytest.skip("requires a system utilization collector")
    # The suite uses tests/config, which intentionally contains no procfs metrics.
    # A fresh process isolates ASTL's initialized state and loads the shipped config.
    env = dict(os.environ)
    env["ASTL_CONFIG_DIR"] = str(Path(__file__).resolve().parents[2] / "config")
    env["ASTL_COLLECTORS"] = "procfs"
    result = subprocess.run([sys.executable, "-c", """
import astl
system_targets = [target for target in astl.get_targets() if target.name == "sysutil"]
assert len(system_targets) == 1
metrics = {metric.name for metric in astl.get_metrics(system_targets[0])}
assert {"memory.total", "memory.used", "memory.utilization",
        "cpu.total.utilization", "cpu.0.utilization"} <= metrics, metrics
assert not {"meminfo.MemTotal", "meminfo.MemUsed", "meminfo.utilization",
            "stat.cpu.utilization", "stat.cpu0.utilization"} & metrics
"""], env=env, capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
