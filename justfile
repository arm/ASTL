# https://just.systems/man/en/

alias  b := build
alias  t := test
alias rt := retest

# generate build files through cmake
config preset='debug':
    cmake -S . --preset {{preset}}

# build library, samples, unit tests
build preset='debug':
    cmake --build . --parallel=8 --preset {{preset}}

# format source code (C/C++ & related) using repository script
format:
    ./scripts/format.sh

# run lint/static analysis step; pass build directory and mode (e.g. pull-request)
# NOTE: assumes scripts/lint.sh usage: lint.sh <build-dir> <mode>
#       and that the build directory follows build/<preset> naming.
lint preset='debug':
    ./scripts/lint.sh build/{{preset}} pull-request

# run unit tests
test preset='debug': build
    ctest -LE "integration" --preset {{preset}}

# test everything, generate html coverage file
test-cov: build
    ./scripts/run_tests_and_create_html_cov.sh

retest preset='debug':
    ctest --rerun-failed --preset {{preset}}
