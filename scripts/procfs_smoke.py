#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0
"""Live procfs comparison. Run with the built ASTL Python binding and procfs collector."""
from __future__ import annotations

import json
import math
import os
import shutil
import subprocess
import time
from pathlib import Path

os.environ.setdefault("ASTL_COLLECTORS", "procfs")
import astl


def command(*args: str) -> str:
    return subprocess.check_output(args, text=True, env={**os.environ, "LC_ALL": "C"})


def swap_reference() -> dict[str, float]:
    result: dict[str, float] = {}
    for line in command("free", "--bytes").splitlines():
        fields = line.split()
        if fields and fields[0] == "Swap:":
            total, used = map(int, fields[1:3])
            result["swap.total"] = total
            result["swap.used"] = used
            result["swap.occupancy"] = 100 * used / total if total else 0
    return result


def scheduler_reference() -> dict[str, float]:
    result: dict[str, float] = {}
    for line in command("vmstat", "-s").splitlines():
        fields = line.split()
        if "CPU context switches" in line:
            result["scheduler.context_switches"] = int(fields[0])
        elif "interrupts" in line:
            result["scheduler.interrupts"] = int(fields[0])
    return result


def network_reference() -> dict[str, float]:
    result: dict[str, float] = {}
    interfaces = []
    if shutil.which("ip"):
        try:
            interfaces = json.loads(command("ip", "-j", "-s", "link"))
        except subprocess.CalledProcessError:
            print("SKIP: ip/netlink unavailable; independent network comparison omitted")
    else:
        print("SKIP: ip unavailable; independent network comparison omitted")
    for interface in interfaces:
        stats = interface.get("stats64", interface.get("stats", {}))
        for direction in ("rx", "tx"):
            if direction in stats:
                result[f"network.{interface['ifname']}.{direction}_bytes"] = stats[direction]["bytes"]
    return result


def disk_reference() -> dict[str, float]:
    result: dict[str, float] = {}
    # sysfs independently exposes each disk's counters, also used by iostat.
    for device in Path("/sys/class/block").iterdir():
        fields = list(map(int, (device / "stat").read_text().split()))
        for kind, index, scale in (("read_operations", 0, 1), ("write_operations", 4, 1),
                                   ("read_bytes", 2, 512), ("write_bytes", 6, 512)):
            result[f"disk.{device.name}.{kind}"] = fields[index] * scale
    return result


def reference() -> dict[str, float]:
    return {
        **swap_reference(),
        **{f"scheduler.load_average_{minutes}m": value
           for minutes, value in zip((1, 5, 15), os.getloadavg())},
        "system.current_processes": len(command("ps", "-e", "-o", "pid=").splitlines()),
        "system.current_threads": len(command("ps", "-eL", "-o", "lwp=").splitlines()),
        **scheduler_reference(),
        **network_reference(),
        **disk_reference(),
    }

def comparison_tolerance(name: str, high: float) -> float:
    if name.startswith("system.current_"):
        return max(5, high * 0.1)
    if name.startswith("scheduler.load_average_"):
        return 0.01  # procfs rounds load averages to two decimal places.
    return 0


def screenshot_metrics() -> set[str]:
    """Require the supported system panels as well as independent comparisons."""
    return {
        "cpu.total.utilization", "cpu.total.iowait", "memory.total", "memory.used",
        "memory.utilization", "meminfo.MemAvailable", "swap.total", "swap.used", "swap.occupancy",
        "scheduler.load_average_1m", "scheduler.load_average_5m", "scheduler.load_average_15m",
        "scheduler.runnable_tasks", "scheduler.blocked_tasks", "system.current_processes",
        "system.current_threads", "scheduler.context_switches_per_second", "scheduler.interrupts_per_second",
        "vm.page_faults", "vm.major_page_faults", "vm.page_faults_per_second", "vm.major_page_faults_per_second",
    }


def main() -> None:
    before = reference()
    target = next(t for t in astl.get_targets() if t.name == "sysutil")
    available = {m.name: m for m in astl.get_metrics(target)}
    required = {"swap.total", "swap.used", "swap.occupancy", "scheduler.context_switches", "scheduler.interrupts",
                "system.current_processes", "scheduler.load_average_1m",
                "scheduler.load_average_5m", "scheduler.load_average_15m"} & before.keys()
    missing = (required | screenshot_metrics()) - available.keys()
    assert not missing, f"Required system metrics were not discovered: {sorted(missing)}"
    selected = [m for name, m in available.items() if name in before or name.endswith("_per_second")
                or name.startswith(("cpu.", "memory.", "vm.", "network.", "scheduler."))
                or name == "meminfo.MemAvailable"]
    assert selected, "No system metrics discovered"
    astl.configure_basic_collection(target, metrics=selected)
    astl.start_collection(target)
    try:
        # Task enumeration is non-atomic. Retry if a task exits during a thread scan.
        reads = 0
        for _ in range(10):
            try:
                astl.read_immediate(target)
                reads += 1
                if reads == 3:
                    break
            except astl.ASTLError as error:
                print(f"RETRY: {error}")
            time.sleep(0.25)
        assert reads == 3, "Could not complete three valid collections"
        after = reference()
        compared = 0
        for metric in selected:
            samples = astl.get_metric_samples(target, metric)
            assert samples, f"No samples: {metric.name}"
            value = float(samples[-1][1])
            assert math.isfinite(value) and value >= 0, (metric.name, value)
            if metric.name in before and metric.name in after:
                low, high = sorted((before[metric.name], after[metric.name]))
                # Task scans and independent ps calls observe different instants (and ps itself).
                tolerance = comparison_tolerance(metric.name, high)
                assert low - tolerance <= value <= high + tolerance, (metric.name, low, value, high)
                compared += 1
                print(f"COMPARE {metric.name}: OS=[{low}, {high}] ASTL={value}")
            elif metric.name.endswith((".iowait", ".utilization", ".occupancy")):
                assert value <= 100
            elif metric.name.endswith("_per_second"):
                assert len(samples) <= reads - 1, f"First sample was not a baseline: {metric.name}"
                print(f"RATE {metric.name}: {value}")
        assert compared >= 4, f"Too few independent comparisons: {compared}"
        print(f"PASS: {len(selected)} metrics sampled; {compared} independent OS comparisons; rates and percentages valid")
    finally:
        astl.stop_collection(target)


if __name__ == "__main__":
    main()
