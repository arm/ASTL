#!/usr/bin/env bash

# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

readonly TARGETS=(astl_wrapper_validation_fuzzer astl_discovery_fuzzer)
readonly FUZZ_CC="${CC:-clang}"
readonly FUZZ_CXX="${CXX:-clang++}"

cmake -S . -B build/cflite -G Ninja \
	-DASTL_BUILD_FUZZING=ON \
	-DASTL_BUILD_SAMPLES=OFF \
	-DASTL_BUILD_SHARED=OFF \
	-DASTL_BUILD_TESTING=OFF \
	-DASTL_BUILD_TOOLS=OFF \
	-DASTL_LIBSENSORS=OFF \
	-DASTL_PROCFS=ON \
	-DASTL_SANITIZER=address-undefined \
	-DASTL_USE_VCPKG=OFF \
	-DBUILD_TESTING=OFF \
	-DCMAKE_BUILD_TYPE=RelWithDebInfo \
	-DCMAKE_C_COMPILER="${FUZZ_CC}" \
	-DCMAKE_CXX_COMPILER="${FUZZ_CXX}" \
	-DCMAKE_CXX_SCAN_FOR_MODULES=OFF \
	-DCMAKE_BUILD_RPATH="\$ORIGIN" \
	-DCMAKE_UNITY_BUILD=ON \
	-DCMAKE_UNITY_BUILD_BATCH_SIZE=8 \
	-DProtobuf_USE_STATIC_LIBS=ON \
	-DASTL_FUZZ_DISCOVERY_PROFILE=procfs
cmake --build build/cflite --target "${TARGETS[@]}" --parallel "$(nproc)"

cp -L /usr/lib/x86_64-linux-gnu/libfmt.so.9 "${OUT}/libfmt.so.9"
cp -R config "${OUT}/config"
"${FUZZ_CXX}" --version >"${OUT}/compiler-version.txt"

for target in "${TARGETS[@]}"; do
	binary="build/cflite/x86_64/bin/${target}"
	test -x "${binary}"
	cp "${binary}" "${OUT}/${target}"
	zip -q -j "${OUT}/${target}_seed_corpus.zip" "fuzz/corpus/${target}"/*
	cat >"${OUT}/${target}.options" <<'EOF'
[libfuzzer]
max_len = 4096
max_total_time = 60
timeout = 10
EOF
done
