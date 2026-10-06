<!--
SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>

SPDX-License-Identifier: Apache-2.0
-->

# Android procfs development and CI

ASTL supports native Android executables launched through **unprivileged
`adb shell` with SELinux enforcing**, collecting the guest's real `/proc/stat`
and `/proc/meminfo`. This does not establish procfs access from an ordinary APK,
JNI library, or isolated app process. Those contexts require separate permission
validation. Root and permissive SELinux are not required or used by the smoke test.

## Supported configuration

- Minimum Android API: **28 (Android 9)**. Both ASTL and vcpkg dependencies target
  API 28. Older releases are not covered.
- Runtime CI: **API 28 and 35 (Android 15)**, AOSP `default` system images,
  two virtual cores. This covers the minimum and a second newer release; it is
  not an exhaustive Android version or vendor-device compatibility matrix.
- NDK: **r29, `29.0.14206865`**, with Clang/libc++ and C++23 support.
- ABIs: **arm64-v8a and x86_64** compile in CI; x86_64 executes in CI.
  Use arm64-v8a to execute locally on Apple Silicon. Arm64 hardware is not
  exercised by the Ubuntu x86 emulator.
- C++ runtime: `c++_shared`, deployed with ASTL's shared library. Dependencies are
  built using the repository's pinned vcpkg manifest and Android triplets.
- Collectors: procfs enabled, libsensors disabled, `ASTL_COLLECTORS=procfs`.
  SCMI hardware interfaces are outside this validation.

## Local development on macOS or Linux

Install Java 17, CMake, Ninja, the Android SDK command-line tools, and the NDK.
Android Studio's SDK Manager or `sdkmanager` can install the packages below.
The scripts also require the tools used by `scripts/bootstrap-vcpkg.sh`, including
Git, curl, unzip, and jq. No Gradle project or APK is needed.

```sh
# Set these to your installation paths, and add SDK tools to PATH.
export ANDROID_HOME=/path/to/android-sdk
export ANDROID_NDK_HOME="$ANDROID_HOME/ndk/29.0.14206865"
export PATH="$ANDROID_HOME/platform-tools:$ANDROID_HOME/emulator:$ANDROID_HOME/cmdline-tools/latest/bin:$PATH"

# Match the image and executable ABI to the host for hardware acceleration.
case "$(uname -m)" in
  arm64|aarch64) export ANDROID_ABI=arm64-v8a ;;
  x86_64) export ANDROID_ABI=x86_64 ;;
  *) echo 'Unsupported emulator host architecture'; exit 1 ;;
esac
sdkmanager 'platform-tools' 'emulator' 'ndk;29.0.14206865' \
  "system-images;android-35;default;$ANDROID_ABI"
avdmanager create avd --name astl-api35 \
  --package "system-images;android-35;default;$ANDROID_ABI"
emulator -avd astl-api35 -no-snapshot -cores 2 -memory 2048
```

In another terminal, with the same SDK environment:

```sh
scripts/android/build.sh "$ANDROID_ABI"
adb wait-for-device
# Wait until this prints 1 before testing:
adb shell getprop sys.boot_completed
scripts/android/run.sh "$ANDROID_ABI"
```

Repeat with an API 28 image to verify the supported minimum. Both images need
an ABI matching the host for hardware acceleration: Hypervisor Framework on
macOS, KVM on Linux. MacBook development uses the same build and test scripts
as Ubuntu CI. Set `ANDROID_SERIAL` when multiple devices are connected.
`CMAKE_BUILD_PARALLEL_LEVEL` controls build parallelism; `ASTL_ANDROID_LOG_DIR`
can select a separate results directory for each emulator version.

## What the smoke test verifies

`procfs_smoke` links the ASTL shared library and calls its public C API to discover
the procfs target, total/available/used memory, memory utilization, and at least
one per-core CPU utilization metric. It makes four immediate reads 250 ms apart
and checks repeated samples, advancing timestamps, expected units, finite
percentages in [0, 100], positive total memory, and memory readings bounded by
total memory. It allows the initial CPU baseline and does not require a specific
CPU load or exact memory value.

The deployed configuration contains only `metrics/procfs/metrics.json` and the
metric group catalog. Procfs selection does not depend on a platform lookup
or matching a device UUID. The shared metric definitions also serve non-Android
Linux; a fixture regression test covers this configuration without SCMI files.

The runner script checks shell UID 2000 and enforcing SELinux, clears any procfs
root override, and reads the live `/proc`. Output under
`build/android-<ABI>/test-results/` includes ASTL readings, execution context,
raw procfs files, and logcat; CI uploads these artifacts even when collection
fails. The raw procfs files are diagnostic snapshots taken after the test, so
memory values need not exactly match earlier ASTL samples.

CI builds each ABI once, then tests the x86_64 bundle on both API levels.
Tool versions and system-image API/variant are explicit; SDK repositories can
update emulator and image revisions, which are recorded in SDK package metadata.
GitHub Actions/KVM setup is the only portion specific to Ubuntu.
