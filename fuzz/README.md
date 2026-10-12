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

## Source-line coverage

The [Fuzz testing workflow](https://github.com/Arm-Debug/ASTL/actions/workflows/clusterfuzzlite-pr.yml)
reports the union of executable lines reached by the canonical targets under
`src/`, including implementation headers. It runs on relevant pull requests,
pushes to `main`, and manual dispatch. The workflow keeps mutation testing in
`fuzz` and canonical corpus replay in an independent `coverage` job. Both jobs
run on relevant PRs and manual dispatch; pushes to `main` run coverage and badge
publication without another mutation session. A crash in `fuzz` does not skip
`coverage`. The jobs use separate builds and do not share newly discovered inputs;
coverage replays the checked-in seeds plus the optional evolved-corpus artifact.

The run summary shows the percentage and per-file totals; its `fuzz-coverage`
artifact contains:

- `html/index.html`: annotated source with covered and uncovered lines.
- `uncovered.json`: repository-relative paths, source hashes, uncovered line
  numbers and source text, per-file totals, revision, compiler, fixture profiles,
  and SHA-256 manifests of replayed inputs. Use this alongside the matching source
  revision to guide harness generation.
- `coverage.xml`: artifact-only Cobertura report for download and external tools.
  Fuzz coverage is not uploaded to GitHub Code Quality and does not contribute
  to the test coverage merge threshold.
- `coverage.info`, `summary.md`, and `badge.svg`: LCOV, a text summary, and a
  percentage badge. The README displays this compact SVG alongside the other
  badges and links to public metadata on `arm/ASTL`'s shared `badges` branch.
  The `Publish public badges` workflow publishes `fuzz-coverage.svg` and
  `fuzz-coverage.json` from a successful non-PR coverage run for the exact commit
  on public `main`. It runs after fuzz testing or mirroring completes, with daily
  and manual retries. Metadata records the revision, corpus mode, percentage,
  and timestamps; full annotated reports remain in the workflow artifacts.
  Missing reports preserve the previous badge, and PR runs do not publish badges.

This percentage is **covered executable lines / measured executable lines**,
not physical lines including comments and whitespace. Coverage mappings come
from a zero-count baseline of the entire ASTL static archive, combined with
the linked fuzz binaries, so compiled code not linked into a harness still counts
as uncovered. Files outside `src/` are excluded. Files without LLVM
mappings (for example disabled libsensors implementations and declaration-only
headers) appear in `unmeasured_files` in JSON and are outside the denominator.
Conditional code excluded by the build is also outside the denominator.

### Excluding files

Edit [`coverage_exclusions.txt`](coverage_exclusions.txt) to exclude files from
all report formats and from both sides of the coverage percentage. Each entry is
an exact repository-relative `src/` path followed by `#` and a reason; blank lines
and comments are allowed. Add future exclusions here. The initial exclusion is
`src/astl_test_hooks.cpp`, which contains unit-test-only hooks. JSON records these
paths and reasons in `excluded_files`, separately from `unmeasured_files`.
Invalid, missing, or duplicate paths fail the report rather than silently doing
nothing.

### Build scope and replay

This is Linux coverage with procfs enabled and libsensors disabled, not a claim
of coverage across all platform configurations. A covered line can still have
uncovered branches; this report measures line coverage only.

Replay uses `-runs=0` without mutation, forces discovery's `procfs` profile, and
uses wrapper validation's `synthetic-wrapper` fixture. Host-dependent and mixed
SCMI runs do not contribute to this percentage. Fresh profiles are merged across
both targets before reporting; a replay crash or missing profile fails the job.
LLVM can warn about zero-hash placeholders for unused inline functions when a
live definition exists elsewhere. The baseline retains those lines, while the
linked mappings supply their hits. Diagnostics are recorded in JSON; nonzero
function-hash mismatches fail the report.

Until nightly corpus preservation is available, automatic runs explicitly report
`seeds`. For `seeds+evolved`, manually dispatch with `corpus-run-id` identifying a
run in this repository that uploaded `canonical-fuzz-corpus`, containing nonempty
`astl_discovery_fuzzer/` and `astl_wrapper_validation_fuzzer/` directories. Both
checked-in seeds and evolved inputs are replayed. A requested but missing or
incomplete artifact fails rather than silently falling back to seeds.

To reproduce the seed report on an x86_64 Docker host:

```sh
just fuzz-coverage
# Optionally include evolved inputs (one subdirectory per canonical target):
just fuzz-coverage /path/to/corpus
```

Requires Docker with Linux container support and `just`. The command builds the
same image as CI, replays the corpora, and writes reports to `coverage/fuzz/`.
Open `coverage/fuzz/html/index.html` for annotated source, or use
`coverage/fuzz/uncovered.json` as input for harness generation. The image targets
Linux x86_64; ARM hosts need Docker's amd64 emulation. The initial image build
requires network access and downloads the compiler and dependencies.

For a local evolved corpus, also mount its root and pass `--corpus /corpus` to
`scripts/fuzz_coverage.py`. The script defaults to `llvm-profdata` and `llvm-cov` on `PATH`; use
`--llvm-profdata` and `--llvm-cov` to select explicit paths. Use matching Clang, `llvm-profdata`, and `llvm-cov`
versions (the container uses LLVM 20). Artifacts are retained for 30 days.
