<!--
SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>

SPDX-License-Identifier: Apache-2.0
-->

# ASTL Changelog

All notable user-facing changes to the ASTL public API are recorded here.

## Unreleased

### Added

- Added raw-to-UTC clock correlation saved with collection archives and exposed through C, Python, and Go APIs.
  Native and ATX interval CSV reports include UTC timestamps, and native Perfetto output includes clock correlation metadata.

- Added an optional Windows system collector for aggregate and per-logical-processor CPU busy-time utilization,
  physical memory capacity, used bytes, and utilization through the `sysutil` target.
  Select it with `ASTL_COLLECTORS=windows`, or use `procfs,windows` across platforms;
  known unavailable backends are skipped and selection fails if none are available.

- Added native Android procfs support for API 28+ shell executables, with NDK builds,
  emulator CI, and local Apple Silicon development scripts. The generic procfs
  configuration now also exposes `meminfo.MemAvailable` in bytes.

### Changed

### Breaking

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
