# https://just.systems/man/en/

alias  b := build
alias  t := test
alias rt := retest

# build library, samples, unit tests
build preset='debug':
    ./scripts/format.sh
    cmake --build . --parallel=8 --preset {{preset}}

# generate build files through cmake
config preset='debug':
    cmake -S . --preset {{preset}}


# run unit tests
test preset='debug': build
    ctest --parallel=8 -LE "integration" --preset {{preset}}

# test everything, generate html coverage file
test-cov: build
    ./scripts/run_tests_and_create_html_cov.sh

retest preset='debug':
    ctest --rerun-failed --preset {{preset}}
