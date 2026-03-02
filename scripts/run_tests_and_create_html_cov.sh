#!/usr/bin/bash

# SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

set -x
# first, delete old coverage files
rm -r coverage*
# note: .gcno files are created at _compile_ time
# they're necessary to generate coverage reports, so don't delete them
find . | grep -E "\.gcda|\.gcov" | xargs rm

# run the tests
ctest --parallel 8 -LE "integration|mocksysfs" --preset debug

# make directories at the places merge_coverage expects them to be?
mkdir -p build/debug/CMakeFiles/astl.dir/src/
mkdir -p build/debug/samples/sample_test/src/CMakeFiles/sample_test.dir/
mkdir -p build/debug/src/impl/CMakeFiles/astl_static.dir/
mkdir -p build/debug/src/impl/CMakeFiles/astl_static.dir/collector/
mkdir -p build/debug/src/impl/CMakeFiles/astl_static.dir/common/
mkdir -p build/debug/src/impl/CMakeFiles/astl_static.dir/config/
mkdir -p build/debug/src/impl/CMakeFiles/astl_static.dir/metric/
mkdir -p build/debug/src/impl/CMakeFiles/astl_static.dir/topology/
mkdir -p build/debug/tests/wrapper_test/src/CMakeFiles/libsensors_collector_test.dir/
mkdir -p build/debug/tests/wrapper_test/src/CMakeFiles/unit_test.dir/
mkdir -p build/debug/tests/wrapper_test/src/CMakeFiles/wrapper_test.dir/

./scripts/merge_coverage.sh build/
./scripts/create_coverage_report.sh --html
