<!--
SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>

SPDX-License-Identifier: Apache-2.0
-->

# Generic Linux and Android procfs metrics

`metrics.json` applies to Linux and Android independently of SoC, CPU architecture,
SCMI UUID, or `platform_lookup.json`. ASTL reads declarations directly from
`$ASTL_CONFIG_DIR/metrics/procfs/*.json`. Deploy this directory and
`config/groups/metric_groups.json`; no device-specific SCMI configuration is needed
when `ASTL_COLLECTORS=procfs`.

The definitions expose total, available, and used memory in bytes, memory
utilization, and aggregate/per-core CPU utilization. Memory fields come from
`/proc/meminfo`. CPU labels are discovered from `/proc/stat`, and utilization uses
the difference between successive CPU snapshots, excluding idle and iowait from
busy time. The first CPU read establishes a baseline.

The process must be allowed to read these files. Android CI uses an unprivileged
`adb shell` executable with SELinux enforcing; ordinary APK processes have
different permissions. See [Android development](../../../doc/android.md).

## Linux system telemetry

These additions are exposed through ASTL's existing metric and raw-counter
discovery and collection APIs. Applications can discover and select the canonical
metric names without additional API calls.

Default discovery now includes the following metrics. Counters are unsigned
cumulative values; names ending in `_per_second` are floating-point interval
rates, emitted as value samples. Other entries below are instantaneous gauges.
The additive public units `ASTL_UNITS_BYTESPERSEC` (14) and
`ASTL_UNITS_COUNTPERSEC` (15) preserve bytes/s and events or operations/s without
converting bandwidth to MB/s.

| Default name | Source and field (zero-based tokens unless stated) | Meaning/unit |
| --- | --- | --- |
| `disk.<device>.read_operations`, `write_operations` | `/proc/diskstats`, tokens 3 and 7, selected by device-name token 2 | Completed operations, cumulative; merged requests are not separate completed operations |
| `disk.<device>.read_bytes`, `write_bytes` | `diskstats`, tokens 5 and 9 times **512**, independent of physical sector size | Bytes, cumulative |
| The four disk names with `_per_second` | Counter delta / measured elapsed seconds | IOPS or bytes/s |
| `network.<interface>.rx_bytes`, `tx_bytes` | `/proc/net/dev`, first and ninth fields **after** the colon | Bytes, cumulative, interface identity retained |
| Those network names with `_per_second` | Counter delta / measured elapsed seconds | RX/TX bytes/s |
| `vm.page_faults`, `vm.major_page_faults` | `/proc/vmstat`, `pgfault`, `pgmajfault` | Cumulative faults since boot; major faults are a subset of all faults; rates available as `vm.page_faults_per_second` and `vm.major_page_faults_per_second` |
| `scheduler.context_switches`, `interrupts`, `soft_interrupts` | `/proc/stat`, `ctxt`, first `intr` value, first `softirq` value | Cumulative context switches, hardware/architecture interrupt events, software interrupt executions |
| Those scheduler names with `_per_second` | Counter delta / measured elapsed seconds | Events/s |
| `cpu.<N>.interrupts`, `soft_interrupts`, and their `_per_second` variants | `/proc/interrupts` and `/proc/softirqs`, sum the column labelled `CPU<N>` | Cumulative events and events/s per core; hardware/local and software interrupts remain separate |
| `cpu.total.iowait`, `cpu.<N>.iowait` | `/proc/stat` CPU token 5 delta / CPU total delta times 100 | I/O wait percentage |
| `swap.total` | `/proc/meminfo`: `SwapTotal` | Configured capacity in bytes; zero without swap; checked conversion from kB |
| `scheduler.load_average_1m`, `scheduler.load_average_5m`, `scheduler.load_average_15m` | `/proc/loadavg`, tokens 0, 1, 2 | Dimensionless system load averages, counting runnable and uninterruptible tasks; not CPU utilization |
| `scheduler.runnable_tasks`, `scheduler.blocked_tasks` | `/proc/stat`: `procs_running`, `procs_blocked` | Instantaneous counts of runnable tasks (running or ready) and tasks blocked waiting for I/O; not process totals |
| `vm.page_faults_per_second`, `vm.major_page_faults_per_second` | `/proc/vmstat`: `pgfault`, `pgmajfault` deltas / measured elapsed seconds | Faults/s; major faults are a subset of all faults; same baseline and reset handling as other rates |
| `swap.used`, `swap.occupancy` | `/proc/meminfo`: `SwapTotal`, `SwapFree` | `max(total-free,0)*1024` bytes; `100*max(total-free,0)/total` percent, preserving fractions; both zero for no swap |
| `system.current_processes`, `system.current_threads` | Numeric directories at the procfs root; sum their `/proc/<pid>/status` `Threads` fields | Visible process (thread-group leader) count and visible thread count, including each leader |

The raw-counter discovery API also exposes `diskstats.<device>.*`,
`netdev.<interface>.*`, `vmstat.pgfault`, `vmstat.pgmajfault`,
`interrupts.CPU<N>`, `softirqs.CPU<N>`, and the two `system.current_*` gauges.
It retains the existing `stat.*`, `meminfo.*`, `loadavg.*`, and `uptime.*`
readers. Swap gauges reuse the meminfo key/value parser with checked native
arithmetic, preserving fractional occupancy and rejecting byte-conversion overflow.
Both swap inputs come from the same meminfo read. Scalar
scheduler/VM reads reuse the existing token parser. Rate lifetime state is held
by the collector alongside CPU utilization so source failures can invalidate the
baseline; the existing value/formula processing is reused downstream. Interrupt
tables are parsed once for all CPU columns during each discovery expansion and
collection pass. Each cached table uses its acquisition timestamp for every
column rate, so later metric processing does not skew the elapsed interval.
Reset detection retains one prior counter per IRQ row and CPU column; its
additional state scales with the size of each interrupt table.

### Aggregation and interrupt coverage

Disk discovery includes whole devices, partitions, virtual and stacked devices.
There is deliberately **no system disk total**: adding a disk and its partitions,
or a device-mapper volume and its backing disks, double counts I/O. Select one
non-overlapping layer and sum only that set. Device names are the identity in
metric names; raw descriptions also record the discovered major:minor numbers.

Network discovery includes loopback and virtual interfaces in the caller's
network namespace. There is **no system network total**. Select the interfaces
appropriate to the observation point; bridges, bonds, veth pairs, tunnels and
physical interfaces may account for the same traffic. RX and TX remain separate.

Per-core hardware interrupt values include the per-CPU numbered IRQ rows and
architecture/local interrupt rows (for example NMI, local timers and IPIs) exposed
by `/proc/interrupts`. Global `ERR` (Arm `Err`)/`MIS` summaries without CPU columns are
excluded. This is not a count of only external device IRQs, nor of interrupt
handling CPU time. Software interrupt executions come exclusively from
`/proc/softirqs`; CPU `irq`/`softirq` time counters are not event counts.
The `/proc/stat intr` aggregate need not equal the sum of available per-core
rows: the kernel exposes different coverage on different architectures.
A masked or empty `/proc/interrupts` produces no discovered hardware metrics.

I/O wait uses the same denominator and aggregate/per-core scope as existing CPU
utilization: delta of user + nice + system + idle + iowait + irq + softirq + steal.
Guest and guest_nice are already included in user/nice and are excluded from the
sum. Aggregate percentages are weighted by CPU time, not summed core percentages.
Linux iowait accounting is approximate and may decrease; such intervals are
suppressed. CPU rows without an iowait field do not advertise an I/O wait
metric, and a missing field during collection suppresses that observation. Existing CPU utilization excludes idle **and** iowait from busy time.

### Sampling, topology changes and unavailable sources

Rates use `std::chrono::steady_clock` measured elapsed seconds, not the requested
sampling interval. The first read establishes a baseline and emits no rate.
A zero or backward interval, counter decrease/reset/wrap, or invalid iowait delta
emits no derived sample and establishes a fresh baseline. Counter wrap is treated
as reset, not guessed from an assumed counter width. A decrease in any individual
IRQ row counter also re-baselines the rates for that interrupt table, even when
other rows increase enough to hide the decrease in the per-core sum. A valid
unchanged counter with positive elapsed time emits zero. Integer subtraction occurs before the
conversion to double for rates. Counter-to-byte conversion and interrupt sums
reject overflow. CPU snapshots reject
duplicate CPU labels. Interrupt tables reject duplicate CPU/IRQ labels, malformed
row labels and incomplete CPU vectors; softirq rows must have exactly one value
per header CPU. These checks prevent partial or doubled sums. Disk rates also re-baseline when the device name maps to a
different major:minor pair. Interrupt rates re-baseline when CPU columns, IRQ
rows or source descriptions change, since the column sum then represents a
different set of counters. Configuration, start and stop clear baselines. Pause/resume
retains them, so resumed rates include the entire elapsed gap, consistent with
existing collector CPU utilization.

An unavailable, malformed, or permission-restricted optional system input is
omitted from discovery, except that task-count gauges only require the procfs root
to be a directory in both raw and default discovery; task-scan errors are deferred
to collection. Swap gauges and composite metrics are omitted when any required
input is unreadable or missing. Task-count scan failures omit only the affected
gauge sample, as described below. A missing configured disk or network label omits
only that label’s sample and clears its rate baseline; reappearance first establishes
a new baseline. Unrelated samples and baselines remain valid. If another configured
source fails during collection, that
batch returns an error and emits no partial raw batch; rate and CPU baselines are
cleared. The next successful read establishes fresh baselines. This includes
shared disk/network file read failures, malformed selected rows, and CPU removal. Recovery/reappearance
therefore cannot bridge that failed observation. Discovery captures the topology
at initialization: newly added devices/interfaces/cores require rediscovery.
Replacement under the same name and disk major:minor (or the same IRQ row
identity) entirely between reads with counters already above the old values
cannot be identified from these procfs counters; restart or
rediscover the collection after known topology changes. Rates do not provide
persistent hardware identity across reboots or renames.

### Current processes and threads

`system.current_processes` counts numeric top-level PID directories, not every
TID. `system.current_threads` sums `Threads` from each such process's status.
These are non-atomic scans: concurrent fork/exit can change the totals. If a status
read fails and its PID directory has disappeared, the collector discards the
entire scan and retries with a fresh process inventory, up to three attempts.
An unreadable status for a still-visible PID (including a permission denial),
a malformed thread count, an unreadable inventory, or exhausted churn retries
omits that gauge sample; no zero, stale value, or partial sum is substituted.
Other procfs metrics continue collecting and retain their CPU/rate baselines.
Task gauges remain discoverable so collection can recover when visibility improves.
Out-of-memory failures still propagate as collection errors.
Status files are opened afresh on every scan to avoid
retaining descriptors for exited tasks after PID reuse. Visible kernel tasks and zombies are included; hidden
processes are not inferred. Counts are scoped to the mounted procfs PID namespace
and visibility policy (`hidepid`, containers, credentials, security policy).
With `hidepid=2`, inaccessible processes may be invisible without an error, so
these are **visible counts**, not guaranteed host totals. An unrestricted host
procfs mount is required to interpret them as current system counts.

`stat.processes` is cumulative task creations since boot; `stat.procs_running`
and `stat.procs_blocked` are runnable and I/O-blocked tasks. None is a current
process total. `loadavg.total_tasks` is the current number of kernel scheduling
entities (threads), not thread groups; its kernel-global accounting can differ
from PID-namespace-visible enumeration. Exact atomic host-wide process/thread
counts under restricted procfs visibility are unsupported. The load averages and
runnable/blocked gauges use kernel accounting and may have a different scope
from the visible PID-namespace process/thread counts.

### Validation

Native fixtures cover device/interface parsing and discovery, hardware versus
software interrupt columns, measured-time rates, resets, invalid intervals,
missing/malformed sources, overflow/wrap, task-count semantics and PID reuse,
IRQ topology changes, disk identity changes, and swap/no-swap with the declared
integer/float output types. Protobuf round trips cover both new rate units.

Python CI runs the live smoke comparison on its Linux Python 3.12 job for pull
requests, pushes to main, and scheduled runs, using the built native library and
Python binding. It installs `procps` and `iproute2` for the independent OS tools.

Run `just test debug` for native and language-binding validation. Run
`ASTL_COLLECTORS=procfs .venv/bin/python scripts/procfs_smoke.py` for a live Linux
comparison against `ps`, `free`, `vmstat`, and (when installed) `ip -j -s link`.
The smoke comparison allows for changes between independently sampled tools.
Disk counters are bracketed with independent sysfs block statistics. If netlink
is denied or `ip` is unavailable, the independent network comparison is skipped;
sysfs network counters may belong to a different network namespace and cannot
safely substitute for netlink in that case.

Source references: [kernel procfs documentation](https://www.kernel.org/doc/html/latest/filesystems/proc.html),
[kernel disk statistics](https://www.kernel.org/doc/html/latest/admin-guide/iostats.html),
[kernel interface statistics](https://www.kernel.org/doc/html/latest/networking/statistics.html),
and [loadavg semantics](https://man7.org/linux/man-pages/man5/proc_loadavg.5.html).
