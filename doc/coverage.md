<!--
SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>

SPDX-License-Identifier: Apache-2.0
-->

# Coverage test execution

The `debug-coverage` preset runs ordinary Catch2 cases in four core-library,
two wrapper, and one optional libsensors shard. The shard mechanism is shared
with Valgrind through `AstlCatchShards.cmake`; the build configurations remain
separate. Normal builds retain individual case discovery.

Coverage discovers `[integration]`, `[time_sensitive]`, and `[valgrind_isolated]`
cases individually instead of adding them to shards. Integration cases run after
coverage reporting, while the other two groups run serially before reporting.
Non-Catch2 tests and optional external tool tests retain their own registrations.
Do not also run ordinary discovered cases in the coverage build: doing so would
double-count their execution. CTest reports shard failures, and Catch2's output
identifies the failing case within the shard.

Use the same selection as Integration CI:

```sh
ctest --parallel 4 -LE 'integration|time_sensitive|valgrind_isolated|^valgrind$' --preset debug-coverage
ctest --parallel 1 -L 'time_sensitive|valgrind_isolated' -LE integration --preset debug-coverage
./scripts/merge_coverage.sh build/debug-coverage
./scripts/create_coverage_report.sh --xml
python3 scripts/check_coverage_shards.py build/debug-coverage
```

The partition check compares the cases listed by each binary with those selected
by its actual CTest commands, including the separately registered cases. It fails
if a case is missing or appears more often than in the binary's test catalogue.
It runs after the report is finalized because listing cases also executes the
instrumented binary and creates profiles; those profiles must not enter the report.

Start with clean coverage data when measuring a new run. Each process writes to
its own `coverage_%p` directory. GCC builds also use atomic profile updates to
protect counters shared by threads inside a process. Neither measure proves that
all GCC negative-counter defects are eliminated; gcovr parsing remains strict.

The merge script reports its input directory count, indexing time, and total
elapsed time. Compare both test invocations and the merge step against the
previous workflow; the targets are under ten minutes for testing and under two
minutes for merging. Fewer process profiles should reduce both runtime writes
and offline merging, without dropping test cases or narrowing measured sources.
