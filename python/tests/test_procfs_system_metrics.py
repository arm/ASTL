# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0
"""Exercise procfs discovery and collection through the built Python binding."""
import os
from pathlib import Path
import subprocess
import sys

import pytest


@pytest.mark.skipif(sys.platform != "linux", reason="Linux procfs collector")
@pytest.mark.parametrize("swap_total,swap_free", [(0, 0), (3, 2)])
def test_procfs_system_metric_collection(tmp_path, swap_total, swap_free):
    root = Path(__file__).resolve().parents[2]
    proc = tmp_path / "proc"
    proc.mkdir()
    (proc / "meminfo").write_text(
        f"MemTotal: 100 kB\nMemAvailable: 50 kB\nSwapTotal: {swap_total} kB\nSwapFree: {swap_free} kB\n")
    (proc / "stat").write_text("cpu 1 0 1 10 2 0 0 0\ncpu0 1 0 1 10 2 0 0 0\nctxt 123\n")
    (proc / "vmstat").write_text("pgfault 100\npgmajfault 3\n")
    code = """
import astl
import math
import sys
total, free = map(int, sys.argv[1:])
assert astl.Units.BYTESPERSEC == 14
assert astl.Units.COUNTPERSEC == 15
target = next(t for t in astl.get_targets() if t.name == 'sysutil')
metrics = {m.name: m for m in astl.get_metrics(target)}
selected = [metrics[n] for n in ('swap.used', 'swap.occupancy', 'vm.page_faults', 'scheduler.context_switches')]
astl.configure_basic_collection(target, metrics=selected)
astl.start_collection(target)
try:
    astl.read_immediate(target)
    expected = {'swap.used': (total-free)*1024, 'swap.occupancy': 100*(total-free)/total if total else 0,
                'vm.page_faults': 100, 'scheduler.context_switches': 123}
    for metric in selected:
        samples = astl.get_metric_samples(target, metric)
        assert len(samples) == 1, (metric.name, samples)
        assert math.isclose(samples[0][1], expected[metric.name], rel_tol=1e-12)
finally:
    astl.stop_collection(target)
"""
    subprocess.run([sys.executable, "-c", code, str(swap_total), str(swap_free)], check=True, env={**os.environ,
                   "ASTL_CONFIG_DIR": str(root / "config"), "ASTL_PROCFS_ROOT": str(proc),
                   "ASTL_COLLECTORS": "procfs"}, timeout=60)
