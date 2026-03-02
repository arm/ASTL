# SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

# https://just.systems/man/en/

alias  b := build
alias  t := test
alias rt := retest

clean:
    #!/usr/bin/env bash
    set -eu -o pipefail
    echo "[clean] Removing build artifacts"
    rm -rf build coverage_* python/build

# Aggressively remove cache files and as many artifacts as possible
deep-clean: clean
    #!/usr/bin/env bash
    set -eu -o pipefail
    echo "[purge] Removing additional artifacts"
    rm -rf external/vcpkg doc/html python/docs/_build/ python/astl/__pycache__/ python/astl/__pycache__/ .mypy_cache .venv python/astl.egg-info/ python/dist/ python/astl/_core.cpp

# generate build files through cmake
config preset='debug':
    #!/usr/bin/env bash
    set -eu -o pipefail
    echo "[config] Using preset={{preset}}"
    cmake -S . --preset {{preset}}

# build library, samples, unit tests
# just config must be run first as a one-time step
build preset='debug': (config preset)
    #!/usr/bin/env bash
    set -eu -o pipefail
    if [ ! -d build/ ]; then
        echo "[build] Build directory missing; running config step first" > /dev/stderr
        exit 1
    fi
    echo "[build] Using preset={{preset}}"
    cmake --build --preset {{preset}} --parallel=$(nproc)

# format source code (C/C++ & related) using repository script
format:
    #!/usr/bin/env bash
    set -eu -o pipefail
    ./scripts/format.sh

# run lint/static analysis step; pass build directory and mode (e.g. pull-request)
# NOTE: assumes scripts/lint.sh usage: lint.sh <build-dir> <mode>
#       and that the build directory follows build/<preset> naming.
lint preset='debug':
    #!/usr/bin/env bash
    set -eu -o pipefail
    ./scripts/lint.sh build/{{preset}} pull-request

license-lint:
    #!/user/bin/env bash
    set -eu -o pipefail
    ./scripts/license_lint.sh

# run unit tests
test preset='debug': build
    #!/usr/bin/env bash
    set -eu -o pipefail
    echo "[test] Using preset={{preset}}"
    ctest -LE "integration" --preset {{preset}}

# test everything, generate html coverage file
test-cov: build
    #!/usr/bin/env bash
    set -eu -o pipefail
    ./scripts/run_tests_and_create_html_cov.sh

retest preset='debug':
    #!/usr/bin/env bash
    set -eu -o pipefail
    echo "[retest] Using preset={{preset}}"
    ctest --rerun-failed --preset {{preset}}

# Just config must be run first as a one-time step, but does not require a build
doc preset='debug':
    #!/usr/bin/env bash
    set -eu -o pipefail
    if [ ! -d build/ ]; then
        echo "[build] Build directory missing; running config step first" > /dev/stderr
        exit 1
    fi
    echo "[doxygen] Generating documentation"
    cmake --build --preset {{preset}} --target doxygen
    echo "[doxygen] Documentation generated under doc/html/index.html"
    sphinx-build -b html python/docs python/docs/_build/html
    echo "[sphinx] Documentation generated under python/docs/_build/html/index.html"

# Full Python cycle: build native lib, install editable package, refresh vendored headers,
# run tests and sample scripts. Accepts optional python version via PY env var.
python-full-cycle preset='debug':
    #!/usr/bin/env bash
    set -eu -o pipefail
    PYBIN="${PY:-python3}"
    echo "[python-full-cycle] Using Python: $PYBIN"
    echo "[python-full-cycle] Configuring CMake preset={{preset}}"
    cmake -S . --preset {{preset}} -DENABLE_VALGRIND=OFF
    cmake --build --preset {{preset}} --parallel=$(nproc)
    if [ ! -d .venv ]; then $PYBIN -m venv .venv; fi
    source .venv/bin/activate
    python -m pip install --upgrade pip >/dev/null
    # Install with optional pandas if available (non-fatal if it fails)
    python -m pip install -e python[pandas] pytest build mypy >/dev/null 2>&1 || python -m pip install -e python pytest build mypy >/dev/null
    echo "[python-full-cycle] Refresh vendored headers"
    bash python/scripts/vendor_headers.sh --quiet || true
    echo "[python-full-cycle] Running pytest suite"
    pytest -q python/tests || { echo "[python-full-cycle] Tests failed"; exit 1; }
    echo "[python-full-cycle] Running sample scripts"
    for s in python/samples/astl_demo.py python/samples/astl_async_demo.py python/samples/astl_polling_demo.py; do
        echo "[python-full-cycle] Sample: $s"; python "$s" || true; done
    echo "[python-full-cycle] COMPLETE"

# Vendor Python headers only (useful before building an sdist or running tests outside full cycle)
vendor-python-headers:
    #!/usr/bin/env bash
    set -eu -o pipefail
    bash python/scripts/vendor_headers.sh --quiet
    echo "[vendor-python-headers] Done"

# Run Python tests ensuring build + vendoring + editable install
python-pytest preset='debug':
    #!/usr/bin/env bash
    set -eu -o pipefail
    PYBIN="${PY:-python3}"
    echo "[python-pytest] Using Python: $PYBIN"
    cmake -S . --preset {{preset}} -DENABLE_VALGRIND=OFF
    cmake --build --preset {{preset}} -j $(nproc)
    if [ ! -d .venv ]; then $PYBIN -m venv .venv; fi
    source .venv/bin/activate
    python -m pip install --upgrade pip >/dev/null
    python -m pip install -e python pytest build mypy >/dev/null 2>&1 || python -m pip install -e python pytest build mypy >/dev/null
    echo "[python-pytest] Vendoring headers"
    bash python/scripts/vendor_headers.sh --quiet || true
    echo "[python-pytest] Running pytest"
    pytest -q python/tests
    echo "[python-pytest] Running mypy"
    mypy python/astl || { echo "[python-pytest] mypy failed"; exit 1; }

# Build a wheel and smoke test import & version in a clean isolated environment
python-wheel-smoke preset='debug':
    #!/usr/bin/env bash
    set -eu -o pipefail
    PYBIN="${PY:-python3}"
    echo "[python-wheel-smoke] Using Python: $PYBIN"
    cmake -S . --preset {{preset}} -DENABLE_VALGRIND=OFF
    cmake --build --preset {{preset}} --parallel=$(nproc)
    echo "[python-wheel-smoke] Vendoring headers"
    bash python/scripts/vendor_headers.sh --quiet || true
    echo "[python-wheel-smoke] Building wheel"
    rm -rf python/dist
    $PYBIN -m build --wheel python > /dev/null
    WHEEL=$(ls -1 python/dist/astl-*.whl | head -n1)
    echo "[python-wheel-smoke] Built wheel: $WHEEL"
    rm -rf .wheel-smoke-venv
    $PYBIN -m venv .wheel-smoke-venv
    source .wheel-smoke-venv/bin/activate
    python -m pip install --upgrade pip > /dev/null
    python -m pip install "$WHEEL" > /dev/null
    EXPECTED=$(grep -v '^#' VERSION.md | head -n1 | tr -d '\r' | sed 's/[[:space:]]*$//')
    ACTUAL=$(python -c 'import astl; print(astl.__version__)')
    echo "[wheel-smoke] astl.__version__ = ${ACTUAL}"
    echo "[wheel-smoke] expected version  = ${EXPECTED}"
    if [ -n "${EXPECTED}" ] && [ "${ACTUAL}" != "${EXPECTED}" ]; then
        echo "[wheel-smoke][FAIL] Version drift: ${ACTUAL} != ${EXPECTED}" >&2
        exit 2
    fi
    echo "[wheel-smoke] Version OK"
    python -c 'import astl, json; astl.initialize(None); print(str(json.dumps(astl.diagnostics().to_dict(), indent=2))[:400])' || echo "[wheel-smoke] diagnostics failed (non-fatal)" >&2
    echo "[python-wheel-smoke] COMPLETE"

# Interactive development shell: builds selected preset, ensures editable install, activates venv, drops into shell.
python-dev-shell preset='debug':
    #!/usr/bin/env bash
    set -eu -o pipefail
    echo "[python-dev-shell] Configure/build preset={{preset}}"
    cmake -S . --preset {{preset}}
    cmake --build --preset {{preset}} --parallel=$(nproc)
    PYBIN="${PY:-python3}"
    if [ ! -d .venv ]; then $PYBIN -m venv .venv; fi
    source .venv/bin/activate
    python -m pip install --upgrade pip >/dev/null
    python -m pip install -e python >/dev/null
    bash python/scripts/vendor_headers.sh --quiet || true
    echo "[python-dev-shell] Ready. venv active. Type 'exit' to leave."
    exec "$SHELL"

full-ci preset='debug':
    #!/usr/bin/env bash
    set -eu -o pipefail
    START_TS=$(date +%s)
    echo "[full-ci] Starting full pipeline with preset={{preset}}"
    rm -rf build/{{preset}} .venv .wheel-smoke-venv
    cmake -S . --preset {{preset}}
    cmake --build --preset {{preset}} --parallel=$(nproc)
    echo "[full-ci] Running C++ tests"
    ctest --preset {{preset}} -LE integration
    echo "[full-ci] Python env setup & tests"
    PYBIN="${PY:-python3}"
    if [ ! -d .venv ]; then $PYBIN -m venv .venv; fi
    source .venv/bin/activate
    python -m pip install --upgrade pip >/dev/null
    python -m pip install -e python pytest build mypy >/dev/null 2>&1 || python -m pip install -e python pytest build mypy >/dev/null
    bash python/scripts/vendor_headers.sh --quiet || true
    pytest -q python/tests
    mypy python/astl || { echo "[full-ci] mypy failed"; exit 1; }
    echo "[full-ci] Wheel smoke"
    rm -rf python/dist .wheel-smoke-venv
    python -m build --wheel python >/dev/null
    WHEEL=$(ls -1 python/dist/astl-*.whl | head -n1)
    python -m venv .wheel-smoke-venv
    source .wheel-smoke-venv/bin/activate
    python -m pip install --upgrade pip >/dev/null
    python -m pip install "$WHEEL" >/dev/null
    EXPECTED=$(grep -v '^#' VERSION.md | head -n1 | tr -d '\r' | sed 's/[[:space:]]*$//')
    ACTUAL=$(python -c 'import astl; print(astl.__version__)')
    echo "[full-ci] astl.__version__=${ACTUAL} expected=${EXPECTED}"
    if [ -n "${EXPECTED}" ] && [ "${ACTUAL}" != "${EXPECTED}" ]; then
        echo "[full-ci][FAIL] Version drift: ${ACTUAL} != ${EXPECTED}" >&2
        exit 2
    fi
    python -c 'import astl, json; astl.initialize(None); print("[full-ci] diagnostics keys sample:", list(astl.diagnostics().to_dict().keys())[:8])'
    echo "[full-ci] COMPLETE in $(( $(date +%s) - START_TS ))s"

# Reuse pre-built native library (downloaded artifact) to run Python tests & mypy without invoking CMake.
python-reuse-tests:
    #!/usr/bin/env bash
    set -eu -o pipefail
    PYBIN="${PY:-python3}"
    echo "[python-reuse-tests] Python: $("$PYBIN" -V)"
    if [ ! -d build/debug/lib ]; then
        echo "[python-reuse-tests][WARN] build/debug/lib missing"
        ROOT_LIB=$(ls -1 libastl-*.so 2>/dev/null | head -n1 || true)
        if [ -n "${ROOT_LIB}" ]; then
            echo "[python-reuse-tests][HEAL] Found ${ROOT_LIB} at repo root; recreating expected directory"
            mkdir -p build/debug/lib
            mv "${ROOT_LIB}" build/debug/lib/
        fi
    fi

    if [ ! -d build/debug/lib ]; then
        if [ "${ASTL_DISABLE_NATIVE_FALLBACK:-0}" = "1" ]; then
            echo "[python-reuse-tests][ERROR] build/debug/lib still missing and fallback disabled (ASTL_DISABLE_NATIVE_FALLBACK=1)" >&2
            exit 3
        fi
        echo "[python-reuse-tests][HEAL] Performing minimal local configure/build (fallback enabled)" >&2
        cmake -S . --preset debug -DENABLE_VALGRIND=OFF
        cmake --build --preset debug --parallel=$(nproc)
    fi

    if [ ! -d build/debug/lib ]; then
        echo "[python-reuse-tests][ERROR] Unable to establish build/debug/lib after fallback build" >&2
        find build -maxdepth 4 -type f -name 'libastl*' 2>/dev/null || true
        exit 3
    fi
    if [ ! -f .venv ]; then
        echo "[python-reuse-tests] Initializing Python venv"
        "$PYBIN" -m venv .venv;
    else
        echo "[python-reuse-tests] Reusing existing Python venv"
    fi
    source .venv/bin/activate
    python -m pip install --upgrade pip >/dev/null
    # Idempotent install (wheel cache speeds reruns)
    python -m pip install -e python pytest mypy build >/dev/null
    echo "[python-reuse-tests] Vendoring public headers into package"
    bash python/scripts/vendor_headers.sh --quiet || { echo "[python-reuse-tests][ERROR] vendoring headers failed" >&2; exit 5; }
    echo "[python-reuse-tests] Verifying vendored headers present"
    missing=0
    for h in astl.h astl_errors.h astl_telemetry.h astl_utils.h astl_version.h; do
        if [ ! -f "python/astl/include/astl/$h" ]; then
            echo "[python-reuse-tests][ERROR] Missing vendored header: $h" >&2
            missing=1
        fi
    done
    if [ $missing -ne 0 ]; then
        echo "[python-reuse-tests][ERROR] One or more expected headers missing after vendoring" >&2
        ls -1 python/astl/include/astl || true
        exit 6
    fi
    echo "[python-reuse-tests] Checking for libastl-*"
    ls build/debug/lib/libastl-*.so > /dev/null || { echo "[python-reuse-tests][ERROR] libastl not found after venv setup" >&2; exit 4; }
    START_TS=$(date +%s)
    echo "[python-reuse-tests] pytest start"
    pytest -q python/tests
    echo "[python-reuse-tests] mypy start"
    mypy python/astl
    END_TS=$(date +%s)
    echo "[python-reuse-tests] COMPLETE in $((END_TS-START_TS))s"

# Build sdist & wheel, run integrity + wheel smoke + diagnostics + short benchmark (reuse existing native lib)
python-package-and-benchmark:
    #!/usr/bin/env bash
    set -eu -o pipefail
    echo "[python-package-and-benchmark] Start"
    PYBIN="${PY:-python3}"
    if [ ! -d build/debug/lib ]; then
        echo "[python-package-and-benchmark][WARN] build/debug/lib missing"
        ROOT_LIB=$(ls -1 libastl-*.so 2>/dev/null | head -n1 || true)
        if [ -n "${ROOT_LIB}" ]; then
            echo "[python-package-and-benchmark][HEAL] Found ${ROOT_LIB} at repo root; recreating expected directory"
            mkdir -p build/debug/lib
            mv "${ROOT_LIB}" build/debug/lib/
        fi
    fi
    if [ ! -d build/debug/lib ]; then
        if [ "${ASTL_DISABLE_NATIVE_FALLBACK:-0}" = "1" ]; then
            echo "[python-package-and-benchmark][ERROR] build/debug/lib still missing and fallback disabled (ASTL_DISABLE_NATIVE_FALLBACK=1)" >&2
            exit 2
        fi
        echo "[python-package-and-benchmark][HEAL] Performing minimal local configure/build (fallback enabled)" >&2
        cmake -S . --preset debug -DENABLE_VALGRIND=OFF
        cmake --build --preset debug --parallel=$(nproc)
    fi
    if ! ls build/debug/lib/libastl-*.so >/dev/null 2>&1; then
        echo "[python-package-and-benchmark][ERROR] No libastl-*.so under build/debug/lib after heal attempts" >&2
        find build/debug -maxdepth 3 -type f -name 'libastl*' || true
        exit 3
    fi
    echo "[python-package-and-benchmark] Vendoring headers"
    bash python/scripts/vendor_headers.sh --quiet || { echo "[python-package-and-benchmark][ERROR] vendoring headers failed" >&2; exit 4; }
    # Build sdist + wheel (no CMake invocation; relies on prebuilt shared lib)
    rm -rf python/dist
    echo "[python-package-and-benchmark] Building wheel + sdist (isolated venv)"
    rm -rf .pkg-build-venv
    $PYBIN -m venv .pkg-build-venv
    source .pkg-build-venv/bin/activate
    python -m pip install --upgrade pip >/dev/null
    python -m pip install build >/dev/null
    python -m build --sdist --wheel python > build_python_pkg.log 2>&1 || { echo "[python-package-and-benchmark][ERROR] build failed" >&2; tail -n 60 build_python_pkg.log; exit 5; }
    WHEEL=$(ls -1 python/dist/astl-*.whl 2>/dev/null | head -n1 || true)
    SDIST=$(ls -1 python/dist/astl-*.tar.gz 2>/dev/null | head -n1 || true)
    if [ -z "$WHEEL" ] || [ -z "$SDIST" ]; then
        echo "[python-package-and-benchmark][ERROR] Missing wheel or sdist output" >&2
        ls -l python/dist || true
        exit 6
    fi
    echo "[python-package-and-benchmark] Built artifacts: $(basename "$WHEEL"), $(basename "$SDIST")"
    # Smoke test in isolated venv
    rm -rf .pkg-smoke-venv
    $PYBIN -m venv .pkg-smoke-venv
    source .pkg-smoke-venv/bin/activate
    python -m pip install --upgrade pip >/dev/null
    # Record import timing start (simple form to keep just parser happy)
    IMPORT_BEGIN=$(python -c 'import time; print(time.perf_counter())')
    python -m pip install "$WHEEL" >/dev/null
    python -c 'import astl' >/dev/null 2>&1 || true
    IMPORT_END=$(python -c 'import time; print(time.perf_counter())')
    VERSION_EXPECTED=$(grep -v '^#' VERSION.md | head -n1 | tr -d '\r' | sed 's/[[:space:]]*$//')
    VERSION_ACTUAL=$(python -c 'import astl; print(astl.__version__)')
    if [ -n "$VERSION_EXPECTED" ] && [ "$VERSION_EXPECTED" != "$VERSION_ACTUAL" ]; then
        echo "[python-package-and-benchmark][ERROR] Version mismatch expected=$VERSION_EXPECTED actual=$VERSION_ACTUAL" >&2
        exit 7
    fi
    echo "[python-package-and-benchmark] Version OK: $VERSION_ACTUAL"
    DIAG_BEGIN=$(python -c 'import time; print(time.perf_counter())')
    python -c 'import astl, json; astl.initialize(None); print(json.dumps(astl.diagnostics().to_dict())[:400])' > diagnostics_sample.json 2>/dev/null || true
    DIAG_END=$(python -c 'import time; print(time.perf_counter())')
    # Compute timings using awk to avoid multi-line python blocks that confuse just parser
    IMP_TIME_MS=$(awk -v b="${IMPORT_BEGIN}" -v e="${IMPORT_END}" 'BEGIN{printf "%d", (e-b)*1000}')
    DIAG_TIME_MS=$(awk -v b="${DIAG_BEGIN}" -v e="${DIAG_END}" 'BEGIN{printf "%d", (e-b)*1000}')
    printf '{\n  "version": "%s",\n  "import_time_ms": %s,\n  "diagnostics_time_ms": %s,\n  "python": "%s",\n  "wheel": "%s",\n  "sdist": "%s"\n}\n' \
        "${VERSION_ACTUAL}" "${IMP_TIME_MS}" "${DIAG_TIME_MS}" "$(python -V 2>&1)" "$(basename "$WHEEL")" "$(basename "$SDIST")" > benchmark_metrics.json
    echo "[python-package-and-benchmark] Metrics: import=${IMP_TIME_MS}ms diagnostics=${DIAG_TIME_MS}ms"
    {
      echo "Wheel: $(basename "$WHEEL")"
      echo "Sdist: $(basename "$SDIST")"
      echo "Version: ${VERSION_ACTUAL}"
      echo "Import time (ms): ${IMP_TIME_MS}"
      echo "Diagnostics time (ms): ${DIAG_TIME_MS}"
    } > benchmark_output.log
    echo "[python-package-and-benchmark] COMPLETE"


# note, you need coverity/bin on your PATH, and you need an auth file setup.
# https://confluence.arm.com/display/ITINFRA/Cloud+Native+Coverity+User+Guide
scan:
    coverity scan
