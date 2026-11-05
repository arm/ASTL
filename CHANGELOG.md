<!--
SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>

SPDX-License-Identifier: Apache-2.0
-->

# ASTL Changelog

All notable user-facing changes to the ASTL public API are recorded here.

## Unreleased

### Fixed

- Keep published configuration references consistent when confidential files are
  excluded, preventing startup failures with the generated public config.

- Retry complete procfs thread-count scans after observed task exits, and omit
  unavailable task-count samples without disrupting other procfs metrics.

- Fixed Python metric configuration to pass the stored native handle address for both
  target-specific and global collection, rather than the address of its Python integer object.

### Added

- Extended Linux procfs telemetry with disk IOPS and bandwidth, network bandwidth,
  page faults, per-core interrupt counters and rates, current visible process and
  thread counts, load averages, runnable/blocked task counts, page-fault rates,
  I/O wait, scheduler rates, and swap capacity and usage. Added the public
  `ASTL_UNITS_BYTESPERSEC` and `ASTL_UNITS_COUNTPERSEC` units and matching Python
  and Go constants; existing unit values remain unchanged.

- Added collection clock references and readable UTC timestamps to system information and saved collection archives.
  System information groups the numeric reference and UTC string in one optional correlation.
  Live initialization captures a reference, including help and target discovery; fresh collection configuration refreshes it.

- Added Linux NUMA sysfs telemetry on the `numa` target: interval page counts, CPU-local/remote
  allocation percentages, and per-node allocations. Select with `ASTL_COLLECTORS=numa`;
  Android and non-Linux discovery are disabled. See `doc/numa.md` for sampling semantics.

- Added an optional Windows PDH collector for aggregate and per-logical-processor CPU busy-time utilization,
  physical memory capacity, used bytes, and utilization through the `sysutil` target.
  Select it with `ASTL_COLLECTORS=windows_pdh`, or use `procfs,windows_pdh` across platforms;
  known unavailable backends are skipped and selection fails if none are available.

- Added native Android procfs support for API 28+ shell executables, with NDK builds,
  emulator CI, and local Apple Silicon development scripts. The generic procfs
  configuration now also exposes `meminfo.MemAvailable` in bytes.

### Changed

### Breaking

- Group the readable UTC reference inside `astl_clock_correlation_t` and remove the redundant availability flag.
  A null `clock_reference_utc` indicates an unavailable reference. Python and Go expose UTC within the correlation.

- Replaced `astlGetClockCorrelation` and its Python/Go getters with clock reference fields in `astlGetSystemInfo`.
  CSV and Perfetto reports no longer emit UTC columns or clock correlation metadata.

- Rename the Windows PDH backend selector from `windows` to `windows_pdh` and
  the CMake option from `ASTL_WINDOWS` to `ASTL_WINDOWS_PDH`, without aliases.
  Update `ASTL_COLLECTORS` in C, Python, and Go applications and any stored
  backend-specific metric IDs to use the `windows_pdh` prefix. The `sysutil`
  target and portable CPU and memory metric names are unchanged.

- Always embed configuration in the library. Select one source in order:
  `ASTL_CONFIG_DIR`, `config/` beside the library (or statically linked executable),
  then bundled resources. Remove OS-specific user and system config searches.
  Embedded release profiles contain their own selected collateral and require
  no companion ASTL config directory.

- Rename the procfs system target to `sysutil` and expose `cpu.total.utilization`,
  `cpu.N.utilization`, `memory.total`, `memory.used`, and `memory.utilization`.
  The old utilization names are removed without aliases; collector selection
  remains `ASTL_COLLECTORS=procfs`.

- Removed `astlCropMetricSamplesOnTarget` and its Python and Go wrappers. Its crop only affected processed samples in
  memory; rebuilding them from the raw sample cache could restore discarded samples. Use `astlCropSamplesOnTarget`
  (or the corresponding Python or Go wrapper) to crop all samples on a target persistently.
- SCMI Gauge data events now report `ASTL_COUNTER_TYPE_VALUE` through raw counter discovery instead of
  `ASTL_COUNTER_TYPE_COUNT`. SCMI Counter and Event data events report their matching counter types; Histogram
  data events no longer appear as raw counters. Applications relying on the old classification or selecting
  Histogram raw counters must update their handling.

## 0.1.0 - 2026-09-25

### Added

- Added metric event-property discovery APIs in C, Python, and Go, including canonical lifecycle-event mappings.

### Changed

- Python counter-collection configuration now passes native counter handle values instead of Python-object
  addresses, allowing configured collections to start and report lifecycle errors correctly.

### Breaking

- Collection lifecycle error returns are now consistent across collectors. Stop is no longer idempotent in its
  return value: calling `astlStopCollectionOnTarget` or `astlStopCollection` on an already-stopped collection
  returns the recoverable `ASTL_STATUS_COLLECTION_ALREADY_STOPPED` instead of success. The collection remains
  stopped; callers performing best-effort cleanup should handle this status explicitly.
- Start, pause, resume, and stop on an unconfigured collection return `ASTL_STATUS_COLLECTION_NOT_CONFIGURED`.
  Pause/resume on a stopped collection return `ASTL_STATUS_COLLECTION_ALREADY_STOPPED`; stop on a configured
  but never-started collection returns `ASTL_STATUS_COLLECTION_NOT_RUNNING`. A stopped collection must be
  configured again before starting; start without reconfiguration returns `ASTL_STATUS_INVALID_STATE_TRANSITION`.
  Public function signatures are unchanged. Python reports already-stopped errors as `ASTLError`; Go returns
  an `Error` with `StatusCollectionAlreadyStopped`.
- SCMI counter names now use the metadata-qualified `component.instance.name` form. Applications selecting counters
  by name must replace unqualified names such as `ENERGY_COUNTER` with names such as `SOC.0.ENERGY_COUNTER`.
- Removed the public `astl_lifecycle_event_type_t`; discover lifecycle values and names through the
  metric event-property APIs. Serialized event metrics must also use the explicit event-metric payload.

## 0.0.2 - 2026-08-26

### Added

### Changed

- Added PR checks for public API wrapper and changelog updates, plus cumulative SemVer validation for stable releases.

### Breaking
