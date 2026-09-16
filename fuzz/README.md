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

`astl_discovery_fuzzer` generates 1–32 bounded public-API discovery operations.
Each input selects either the minimal procfs profile or the mixed procfs/SCMI
profile. The target rebuilds ASTL from the selected fixture at the start of each
iteration, discovers handles by index, and checks count/getter capacity contracts
and deterministic properties. Configuration, collection, samples, processing,
and persistence are intentionally left to their dedicated targets.

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
  fuzz/corpus/astl_discovery_fuzzer/mixed-profile -runs=1
```
