<!--
SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>

SPDX-License-Identifier: Apache-2.0
-->

# Sanitizer builds

ASTL provides deterministic native test configurations for AddressSanitizer plus UndefinedBehaviorSanitizer and for
ThreadSanitizer. The ASan+UBSan preset supports native Ubuntu aarch64 and x86_64 hosts with upstream LLVM Clang. These
configurations are separate from the ordinary debug, coverage, and Valgrind configurations.

## Prerequisites

Install CMake, Ninja, upstream LLVM Clang and its sanitizer runtimes, `just`, and the normal ASTL build dependencies.
On Ubuntu, the CI configuration uses Clang 20 and `libclang-rt-20-dev`.

## Configure, build, and test

Run the ASan+UBSan configuration with:

```sh
just sanitizer debug-asan-ubsan
```

The preset detects a native aarch64 or x86_64 host and selects the matching sanitizer triplet automatically. It also
rebuilds vcpkg dependencies such as protobuf and Abseil with ASan+UBSan instrumentation. The first run therefore takes
longer and consumes additional disk space than an ordinary debug build.

Run TSan separately with:

```sh
just sanitizer debug-tsan
```

The presets disable procfs support because procfs discovery varies by host. They require libsensors development files
and run the mocked libsensors tests alongside the unit and wrapper suites. Tests tagged `time_sensitive` or
`valgrind_isolated` run serially; the remaining deterministic tests run with bounded parallelism.

Results are written below `build/<preset>/test-results`, and sanitizer diagnostics are written below
`build/<preset>/sanitizer-logs`. ASan enables leak detection where the runtime supports it. UBSan prints stack traces
and does not recover after a finding.

## MockScmi end-to-end test

The public deterministic suite excludes `multithreaded_e2e_test` because it requires the optional MockScmi source,
FUSE, and a mounted mock telemetry filesystem. In a checkout containing MockScmi and its launcher scripts, run it
explicitly with:

```sh
just sanitizer-e2e debug-asan-ubsan
```

The same command accepts `debug-tsan`. The runner selects only SCMI telemetry through the mock mount; procfs remains
disabled by the sanitizer presets.

## MemorySanitizer status

Sanitizer configuration probes whether Clang can compile and link a basic MemorySanitizer program. ASTL does not
provide an MSan preset because a reliable MSan run also requires an instrumented C++ standard library and fully
instrumented binary dependencies. The normal Ubuntu and vcpkg dependency graph does not provide that guarantee, so
enabling MSan only for ASTL would produce misleading uninitialized-read reports.

A future MSan preset must use an instrumented libc++ and rebuild every native dependency with MSan before it can be
treated as supported.

## Suppressions

ASTL has no sanitizer suppression files by default. Do not suppress findings in ASTL-owned code. If a toolchain or
third-party dependency requires a suppression, keep it narrowly scoped, explain it in this document, and review it as
part of the change that introduces it.

## Troubleshooting

- A configuration error requiring upstream LLVM Clang means that GCC or AppleClang was selected.
- Missing sanitizer runtime errors normally mean the matching `compiler-rt` package is not installed.
- ASan/UBSan, TSan, Valgrind, and coverage instrumentation use separate build directories and must not be combined.
- Use the per-process files in `sanitizer-logs` together with CTest's `Testing/Temporary/LastTest.log` to reproduce CI
  failures with the corresponding `just sanitizer` command.
