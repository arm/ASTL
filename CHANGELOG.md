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

### Breaking

- Removed the public `astl_lifecycle_event_type_t`; discover lifecycle values and names through the
  metric event-property APIs. Serialized event metrics must also use the explicit event-metric payload.

## 0.0.2 - 2026-08-26

### Added

### Changed

- Added PR checks for public API wrapper and changelog updates, plus cumulative SemVer validation for stable releases.

### Breaking
