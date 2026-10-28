<!--
SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
SPDX-License-Identifier: Apache-2.0
-->

# Correlating collection timestamps with UTC

ASTL retains sample timestamps in `CLOCK_MONOTONIC_RAW` nanoseconds on Linux.
Each live library initialization and clean collection configuration captures a
fresh wall-clock reference, before collecting samples, by reading raw time,
`CLOCK_REALTIME`, then raw time again. The
midpoint of the two raw reads is paired with realtime. Half the read interval,
rounded up, is saved as `uncertainty_ns`. Clock read failures leave the reference
unavailable. Other platforms use the steady and system clocks.

The reference is saved in `platform_info.astl` inside the collection archive:

```json
"wall_clock_correlation": {
  "raw_ns": 1000000000,
  "realtime_ns": 1792540800000000000,
  "uncertainty_ns": 100
}
```

Convert a sample using integer nanosecond arithmetic:

```text
sample_realtime_ns = reference.realtime_ns + (sample_raw_ns - reference.raw_ns)
```

Interval CSV keeps `timestamp_us,value` and appends `timestamp_utc`, formatted as
`YYYY-MM-DDTHH:MM:SS.nnnnnnnnnZ`. The original raw timestamp remains available.
CSV collection metadata also includes the reference and read uncertainty. The
collection date/time uses the saved UTC reference; it is unknown for legacy
archives rather than showing the analysis machine's current date/time.
Perfetto keeps raw microsecond event timestamps and includes an
`astl_clock_correlation` metadata event. Its `raw_ns` and `realtime_ns` are decimal
strings to preserve nanosecond precision in JSON consumers.

Replay uses the archived reference, including when analysis runs on another
host. Older archives without a reference have an empty UTC column and omit clock
reference metadata. Overflowing conversions also produce an empty UTC field.

Nodes must synchronize their realtime clocks with a common reference, for
example using NTP or PTP. This mapping does not synchronize clocks itself. The
saved uncertainty measures clock read latency only. A fixed reference does not
track later wall-clock steps, frequency corrections, or drift between raw and
realtime clocks; errors can grow during long collections. Configuring a new
collection clears the previous samples and captures a fresh
reference. Configuring additional targets in that collection retains the same
reference. Invalid requests that leave the collection intact retain its reference.

C API consumers can call `astlGetClockCorrelation` after initialization or loading
an archive. Initialize `astl_get_clock_correlation_params_t.size` to
`sizeof(astl_get_clock_correlation_params_t)`, set `flags` to zero, and point
`correlation` to an `astl_clock_correlation_t` output struct.
For example:

```c
astl_clock_correlation_t reference = {0};
astl_get_clock_correlation_params_t params = {0};
params.size = sizeof(params);
params.correlation = &reference;
astl_status_code status = astlGetClockCorrelation(&params);
```

The output fields use signed integer nanoseconds;
`ASTL_STATUS_NOT_SUPPORTED` indicates that no reference is available. Use checked
arithmetic for the conversion formula above. Existing sample APIs continue to
return raw timestamps. The UTC column described above is emitted by the native
library interval CSV writer (`ASTL_OUTPUT_INTERVAL_CSV`).

Python exposes `astl.get_clock_correlation()` as a dictionary with the same three
nanosecond fields. Go exposes `astl.GetClockCorrelation()` with `RawNS`,
`RealtimeNS`, and `UncertaintyNS` fields. Querying the reference triggers lazy
library initialization, just like target
enumeration.

Each native final output emission writes a complete file with that collection's
reference, including empty collections. Reusing an output path for a new
collection replaces its previous report.

The native library interval CSV writer (`ASTL_OUTPUT_INTERVAL_CSV`) and ATX's
metric and raw-counter interval CSV reports all emit
`timestamp_us,value,timestamp_utc`, using the same UTC conversion and formatting.
ATX also uses the saved reference for collection date/time and clock metadata,
including when loading archives; UTC is empty when a reference is unavailable.
The Perfetto metadata described here applies to the native writer selected by
`ASTL_OUTPUT_PERFETTO`; ATX's separate Perfetto writer is unchanged. Perfetto event timestamps
remain raw microseconds; merging traces across nodes requires applying each
node's correlation first. Adding metadata does not automatically align events in
the Perfetto UI.
