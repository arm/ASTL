#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
abi="${1:-arm64-v8a}"
case "$abi" in
arm64-v8a)
	triplet=arm64-android
	ndk_triple=aarch64-linux-android
	;;
x86_64)
	triplet=x64-android
	ndk_triple=x86_64-linux-android
	;;
*)
	echo 'Usage: build.sh [arm64-v8a|x86_64]' >&2
	exit 2
	;;
esac
: "${ANDROID_NDK_HOME:?Set ANDROID_NDK_HOME to NDK 29.0.14206865}"
ndk_version="$(sed -n 's/^Pkg.Revision *= *//p' "$ANDROID_NDK_HOME/source.properties" | tr -d '\r')"
if [[ $ndk_version != 29.0.14206865 ]]; then
	echo "Expected NDK 29.0.14206865, found $ndk_version" >&2
	exit 2
fi
build_dir="$repo_root/build/android-$abi"
vcpkg_dir="${VCPKG_DIR:-$repo_root/external/vcpkg}"
if [[ ! -x "$vcpkg_dir/vcpkg" ]]; then
	(
		cd "$repo_root"
		VCPKG_DIR="$vcpkg_dir" ./scripts/bootstrap-vcpkg.sh
	)
fi
cmake -S "$repo_root" -B "$build_dir" -G Ninja \
	-DCMAKE_TOOLCHAIN_FILE="$vcpkg_dir/scripts/buildsystems/vcpkg.cmake" \
	-DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
	-DVCPKG_TARGET_TRIPLET="$triplet" \
	-DASTL_OUTPUT_ARCHITECTURE="${triplet%-android}" \
	-DANDROID_ABI="$abi" -DANDROID_PLATFORM=android-28 -DANDROID_STL=c++_shared \
	-DCMAKE_BUILD_TYPE=Release -DCMAKE_FIND_PACKAGE_PREFER_CONFIG=ON \
	-DCMAKE_RUNTIME_OUTPUT_DIRECTORY="$build_dir/bin" \
	-DCMAKE_LIBRARY_OUTPUT_DIRECTORY="$build_dir/lib" \
	-DASTL_BUILD_SHARED=ON -DASTL_PROCFS=ON -DASTL_LIBSENSORS=OFF \
	-DASTL_BUILD_TESTING=OFF -DBUILD_TESTING=OFF -DASTL_BUILD_SAMPLES=OFF \
	-DASTL_BUILD_TOOLS=OFF -DASTL_BUILD_PROCFS_SMOKE=ON
cmake --build "$build_dir" --target procfs_smoke --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-4}"

# The bundle deliberately contains no SCMI platform lookup or device-specific definitions.
mkdir -p "$build_dir/bundle/bin" "$build_dir/bundle/lib" \
	"$build_dir/bundle/config/metrics/procfs" "$build_dir/bundle/config/groups"
smoke_binary="$(cat "$build_dir/procfs-smoke-Release-binary.txt")"
library_dir="$(cat "$build_dir/procfs-smoke-Release-library-dir.txt")"
cp "$smoke_binary" "$build_dir/bundle/bin/"
cp -L "$library_dir"/*.so* "$build_dir/bundle/lib/"
ndk_host_dirs=("$ANDROID_NDK_HOME"/toolchains/llvm/prebuilt/*)
ndk_host_dir="${ndk_host_dirs[0]}"
cp "$ndk_host_dir/sysroot/usr/lib/$ndk_triple/libc++_shared.so" "$build_dir/bundle/lib/"
# Keep deployment artifacts small without removing dynamic symbols needed by the shared-library loader.
"$ndk_host_dir/bin/llvm-strip" --strip-debug \
	"$build_dir/bundle/bin/procfs_smoke" "$build_dir/bundle/lib/"*.so*
cp "$repo_root/config/metrics/procfs/metrics.json" "$build_dir/bundle/config/metrics/procfs/"
cp "$repo_root/config/groups/metric_groups.json" "$build_dir/bundle/config/groups/"
echo "Android $abi bundle: $build_dir/bundle"
