#!/usr/bin/env bash

# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

# this script will clone vcpkg, microsoft's C++ package manager.
# we will use the builtin-baseline from vcpkg.json to select the commit to use

set -euo pipefail

VCPKG_DIR="${VCPKG_DIR:-external/vcpkg}"
VCPKG_REPO="https://github.com/microsoft/vcpkg.git"
VCPKG_JSON="${VCPKG_JSON:-vcpkg.json}"

mkdir -p "$(dirname "$VCPKG_DIR")"

if [ ! -d "$VCPKG_DIR/.git" ]; then
	echo "Cloning vcpkg into $VCPKG_DIR"
	git clone "$VCPKG_REPO" "$VCPKG_DIR"
fi

BUILTIN_BASELINE=$(jq -r '.["builtin-baseline"]' "$VCPKG_JSON")

echo "Checking out vcpkg commit $BUILTIN_BASELINE"
git -C "$VCPKG_DIR" fetch --quiet
git -C "$VCPKG_DIR" checkout "$BUILTIN_BASELINE"

echo "Bootstrapping vcpkg..."
"$VCPKG_DIR/bootstrap-vcpkg.sh"

echo "✅ vcpkg is ready"
