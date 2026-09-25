<!--
SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>

SPDX-License-Identifier: Apache-2.0
-->

# ASTL Changelog

All notable user-facing changes to the ASTL public API are recorded here.

## Unreleased

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
