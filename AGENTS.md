<!--
SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>

SPDX-License-Identifier: Apache-2.0
-->

# Repository Guidelines

## Project Structure & Module Organization

Core library code lives in `src/` with public headers under `include/astl/`. Platform and metric definitions are stored in `config/`. Native tests are grouped in `tests/unit_test/`, `tests/wrapper_test/`, `tests/e2e_test/`, and `tests/libsensors_collector_test/`. Language bindings live in `python/astl/` and `Go/astl/`; samples are in `samples/`, `python/samples/`, and `Go/examples/`. Tooling and automation scripts are in `scripts/` and `tools/`.

## Build, Test, and Development Commands

Use CMake presets for native development:

```sh
cmake -S . --preset debug
cmake --build --preset debug
ctest --preset debug
```

Use `just` for the common full-stack workflows:

```sh
just build debug      # configure + build with Ninja
just test debug       # C++, Python, and Go tests
just python-pytest    # pytest + mypy for python/
just go-test debug    # Go smoke tests against build/debug/lib
just format           # apply clang-format
just lint debug       # proto generation + clang-tidy lint
```

## Coding Style & Naming Conventions

C and C++ formatting is enforced by `.clang-format`: 2-space indentation, 120-column limit, left-aligned pointers, and attached braces. Run `just format` before submitting C/C++ changes. Keep public API headers in `include/astl/` and implementation details in `src/impl/`. Python code targets Python 3.10+ and is type-checked with `mypy`; keep pytest files as `test_*.py`. Go tests should follow the usual `*_test.go` convention already used in `Go/astl/`.

## Testing Guidelines

Prefer targeted runs while iterating, then finish with `just test debug`. Native tests are driven by CTest; Python discovery is defined in `python/pytest.ini` with `testpaths = tests` and `python_files = test_*.py`. Existing C++ test names use suffixes such as `_utests.cpp` and descriptive wrapper test files like `summary_api_tests.cpp`. Add regression coverage alongside the affected layer. `tests/unit_test/` tests are made for increasing code coverage and use trompeloeil to mock interfaces. `tests/wrapper_test/` tests are made to exercise the wrapper layer mostly in `src/astl_telemetry.cpp` that converts the C API into calls on the statically linked C++ library. `tests/e2e_test/` are end-to-end tests meant to sense-check the user experience.

## Commit & Pull Request Guidelines

Recent history uses concise, imperative subjects with issue tags, for example `[ASTL-314] Add read-immediate subcommand to atx` or `[NO-JIRA] Add pre-commit hook to auto-format code`. Follow that format and keep unrelated changes out of the same commit. PRs should describe the behavior change, reference the ticket (e.g. [ASTL-123] in the title) or rationale, and list validation performed (`ctest`, `just python-pytest`, `just go-test debug`, etc.). Include logs or screenshots only when they clarify docs, tooling, or user-facing output.

## Configuration Tips

ASTL resolves runtime configuration from `ASTL_CONFIG_DIR`; SCMI development can also override the telemetry mount with `ASTL_SCMI_SYSFS_TELEMETRY_ROOT`. Do not hardcode local paths in tests or samples.
