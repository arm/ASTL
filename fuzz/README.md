<!--
SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>

SPDX-License-Identifier: Apache-2.0
-->

# ASTL fuzz targets

Public-API fuzz targets should keep ASTL's C API, orchestrator, topology, metric
discovery, metric management, and collectors real. Only the operating-system and
hardware boundaries are replaced:

- Link `astl_fuzz_fixtures` and keep one `astl::fuzz::MinimalProcfsFixture`
  alive for the fuzzing process. Call `Reset()` before each iteration, then set
  `ASTL_PROCFS_ROOT` to `RootPath()` before ASTL is initialized.
- Use the existing `tools/mock_scmi` submodule for SCMI. Fuzz builds compile its
  `MockScmi` target and expose `ASTL_FUZZ_MOCK_SCMI_BINARY` and
  `ASTL_FUZZ_MOCK_SCMI_CONFIG` to fixture consumers. Launch it with the existing
  `tools/mock_scmi/scripts/launch_mockscmi.sh` helper, then select its
  `arm_telemetry` directory through `ASTL_SCMI_SYSFS_TELEMETRY_ROOT`. Use the
  matching cleanup script when the process exits.

The procfs fixture deliberately contains only `stat`, `meminfo`, `loadavg`, and
`uptime`. It uses regular files, needs no mount or privileges, and makes the tree
read-only after construction and after every reset.

## Discovery target

`astl_discovery_fuzzer` generates 1–32 bounded public-API discovery and collection-lifecycle operations.
Each input selects either the minimal procfs profile or the mixed procfs/SCMI
profile. The target rebuilds ASTL from the selected fixture at the start of each
iteration, discovers handles by index, and checks count/getter capacity contracts
and deterministic properties. It also configures counters, metrics, and metric
groups at global and per-target scope, then exercises immediate reads, start,
start-paused, pause, resume, reconfigure, and stop in both valid and invalid
orders. Procfs values advance only through an explicit input operation, and each
iteration forcibly stops and clears partial collection state before checking that
the cached fixture is reusable. Sample retrieval, cropping, histograms, and
persistence remain outside this target.

Build and run a bounded mixed-fixture session with the checked-in seeds:

```sh
cmake -S . --preset fuzz
cmake --build --preset fuzz --target astl_discovery_fuzzer
mkdir -p build/fuzz/artifacts/discovery
./fuzz/run_astl_discovery_fuzzer.sh \
  fuzz/corpus/astl_discovery_fuzzer \
  -max_total_time=30 \
  -timeout=2 \
  -max_len=4096 \
  -artifact_prefix=build/fuzz/artifacts/discovery/
```

Replay one seed or saved artifact without mutation:

```sh
./fuzz/run_astl_discovery_fuzzer.sh \
  fuzz/corpus/astl_discovery_fuzzer/repeat-discovery -runs=1
```

Set `ASTL_FUZZ_FIXTURE_PROFILE` to `procfs` or `procfs-scmi` to pin the
discovery target to one deterministic fixture profile. The default, `input`,
keeps both profiles reachable from the input byte stream. Pull-request CI uses
the canonical `procfs` profile; the wrapper-validation target uses its internal
`synthetic-wrapper` profile.

Pull requests that change fuzz-relevant code run each target for up to 60
seconds through ClusterFuzzLite on Ubuntu x86_64. The checked-in seed corpus is
used when no evolved corpus is available, inputs are capped at 4096 bytes, and
each input has a 10-second timeout. Crash inputs are minimized and uploaded by
ClusterFuzzLite; a companion artifact records the target, profile, compiler,
and ASTL revision. The job does not require corpus-storage secrets, including
for pull requests from forks.
