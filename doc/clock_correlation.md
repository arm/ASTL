<!--
SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
SPDX-License-Identifier: Apache-2.0
-->

# Collection clock references in system information

## Native SCMI clock mapping

Completing metric or counter configuration establishes the mapping from each
collector's native timestamps to `CLOCK_MONOTONIC_RAW`. That mapping stays fixed
through immediate reads, start, and pause/resume; reconfiguration establishes a
new mapping after clearing the affected sample history.

For SCMI ioctl hardware timestamps, ASTL makes twenty read attempts per configured
DE. Attempts are interleaved across DEs and spaced at half the active firmware
update interval, using the DE's group interval when applicable. The interval is
read from firmware, independently of the collection sampling interval, and the
half-interval spacing is rounded up to whole nanoseconds.
For a reported 1.2 ms interval, the nineteen scheduled gaps total 11.4 ms, with
read and scheduling overhead potentially extending configuration. The internal
`kClockCorrelationReadAttempts` constant controls the attempt count.

Each hardware read is immediately paired with a raw-clock reading. Among valid
successful reads, ASTL selects the smallest signed offset:
`CLOCK_MONOTONIC_RAW - converted firmware timestamp`. Negative offsets are
compared by signed value, not absolute magnitude. Duplicate timestamps remain
eligible; equal offsets retain the earliest observation. Sensor values do not
participate in selection.

Failed reads, zero timestamps, and duplicates consume attempts; ASTL does not
retry beyond twenty attempts. One usable pair retains its original mapping.
No usable samples, unavailable or invalid intervals, and backward timestamps
fail configuration. Diagnostics report attempts, distinct timestamps, and
failed/zero-timestamp reads for each DE.

The selected pair supplies the existing native/raw anchors and archive
representation. Offset comparisons use exact, bounded integer arithmetic,
without multiplying absolute clock epochs. The stored conversion ratio is
reduced to avoid redundant scaling during normalization of long captures.
Selection state is local to configuration; subsequent acquisitions reuse the
completed mapping. Software-clock mode and legacy sysfs behavior are unchanged.

For a stable clock relationship, stale firmware samples and read delays increase
the observed offset. Minimum selection reduces that positive bias, but an
unknown residual delay remains; it does not establish absolute clock alignment.

## Raw-clock to wall-clock reference

ASTL samples retain `CLOCK_MONOTONIC_RAW` timestamps on Linux. Live initialization
captures a reference connecting raw time to
`CLOCK_REALTIME`. It reads raw time, realtime, then raw time again, and pairs the
raw midpoint with realtime. Half the read interval, rounded up, is saved as
`uncertainty_ns`. Other platforms use steady and system clocks.

ASTL does not record a reference at machine boot. Live initialization, including
through target discovery, captures a reference. Reading system information
alone does not initialize the collection engine, so it has no reference before
initialization or archive loading. Fresh collection configuration refreshes the
reference. Additional targets share the collection reference; a new clean collection captures
a fresh reference. Invalid requests that retain the collection retain its reference.

## Retrieve the reference

Use `astlGetSystemInfo`; there is no separate clock getter:

```c
astl_platform_props_t info = {0};
info.size = sizeof(info);
astl_get_system_info_params_t params = {0};
params.size = sizeof(params);
params.system_info = &info;
if (astlGetSystemInfo(&params) == ASTL_STATUS_SUCCESS &&
    info.clock_correlation.clock_reference_utc != NULL) {
  /* info.clock_correlation contains raw_ns, realtime_ns, uncertainty_ns.
     info.clock_correlation.clock_reference_utc is the reference's ISO 8601 UTC timestamp. */
}
```

Before initialization, if capture fails, or for legacy archives without a reference,
the numeric fields are zero and `clock_correlation.clock_reference_utc` is NULL.
Availability must be checked; zero is a valid time.
UTC strings use `YYYY-MM-DDTHH:MM:SS.nnnnnnnnnZ`.

Default system-info requests return the active collection's reference, including
during archive replay. Explicit `ASTL_SYSTEM_INFO_FLAG_HOST` requests return the
last live collection reference from this process, never the loaded archive's
reference. Explicit `ASTL_SYSTEM_INFO_FLAG_LOADED_SESSION` requests return the
loaded archive's reference and require loaded platform information.

Python exposes `astl.get_system_info()['clock_correlation']` as a dictionary with
three integer nanosecond fields and a `clock_reference_utc` string, or `None` when
unavailable. Go exposes `GetSystemInfo().ClockCorrelation` as a pointer or nil;
its `ClockReferenceUTC` field contains the readable UTC reference.

## Archive persistence and conversion

The reference is saved in `platform_info.astl` inside the `.astl` archive:

```json
"wall_clock_correlation": {
  "raw_ns": 1000000000,
  "realtime_ns": 1792540800000000000,
  "uncertainty_ns": 100,
  "utc": "2026-10-21T00:00:00.000000000Z"
}
```

Convert a sample using checked integer nanosecond arithmetic:

```text
sample_realtime_ns = reference.realtime_ns + (sample_raw_ns - reference.raw_ns)
```

The numeric reference is authoritative; the readable UTC value is derived from
`realtime_ns`. Older archives remain readable without assigning the analysis
host's clock reference to their samples.

UTC information is confined to system information and collection archives.
Native CSV reports keep `timestamp_us,value`; Perfetto timestamps remain
raw microseconds. Reports do not include UTC timestamps or correlation metadata.
Existing report date/time metadata is local time, separate from the reference.

## Accuracy and limitations

This is a basic reference scheme for converting raw monotonic timestamps to UTC
when needed. It does not guarantee accurate alignment indefinitely or synchronize
clocks between nodes. Cross-node comparison depends on each host's UTC clock
being synchronized, for example using NTP or PTP; any host synchronization error
also affects the converted timestamps.

The saved `uncertainty_ns` measures read latency only, not UTC clock accuracy or
accumulated conversion error. A fixed reference cannot account for subsequent
wall-clock steps, NTP frequency corrections, or drift between raw and realtime
clocks, so conversion errors may grow over time.

The scheme also does not handle system suspend/resume: on Linux,
`CLOCK_MONOTONIC_RAW` stops during suspend while realtime continues to advance.
The saved reference therefore cannot correctly map samples taken after resume
without capturing a new reference. A boot-time reference would have the same
limitations.
