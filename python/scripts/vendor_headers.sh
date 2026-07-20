#!/usr/bin/env bash

# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail
#
# vendor_headers.sh (relocated to python/scripts/)
# Copies public ASTL headers (and generated astl_version.h) into the Python package
# so that building a wheel from an sdist works without the full repo layout.
#
# Steps:
# 1. Ensure a generated astl_version.h exists (trigger a lightweight CMake configure if needed).
# 2. Copy headers from include/astl plus generated build/include/astl/astl_version.h
#    into python/astl/include/astl.
# 3. Optionally update the VERSION macro in generated header if a VERSION file changes.
#
# Usage:
#   python/scripts/vendor_headers.sh [--build-dir build/debug] [--quiet]
#
# This script is idempotent; existing vendored headers are overwritten.
#

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")"/../.. && pwd)"
BUILD_DIR="${ROOT_DIR}/build/debug"
QUIET=0

while [[ $# -gt 0 ]]; do
	case "$1" in
	--build-dir)
		BUILD_DIR="$2"
		shift 2
		;;
	--quiet)
		QUIET=1
		shift
		;;
	*)
		echo "Unknown arg: $1" >&2
		exit 2
		;;
	esac
done

log() { ((QUIET)) || echo "[vendor_headers] $*" >&2; }

# Ensure build directory has generated headers (astl_version.h)
if [[ ! -f "${BUILD_DIR}/include/astl/astl_version.h" ]]; then
	log "astl_version.h not found in ${BUILD_DIR}; configuring minimal CMake build"
	cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Debug >/dev/null
	cmake --build "${BUILD_DIR}" --target astl_version_header || true
fi

GEN_VERSION_H="${BUILD_DIR}/include/astl/astl_version.h"
if [[ ! -f ${GEN_VERSION_H} ]]; then
	log "ERROR: Failed to locate generated astl_version.h after build." >&2
	exit 1
fi

SRC_PUBLIC_DIR="${ROOT_DIR}/include/astl"
VENDORED_DIR="${ROOT_DIR}/python/astl/include/astl"
mkdir -p "${VENDORED_DIR}"

# Copy static public headers
shopt -s nullglob
for h in "${SRC_PUBLIC_DIR}"/*.h; do
	base_name="$(basename "$h")"
	# Skip generated version placeholder template
	if [[ ${base_name} == "astl_version.h.in" ]]; then
		continue
	fi
	# Exclude internal / test hook header from vendoring
	if [[ ${base_name} == "astl_test_hooks.h" ]]; then
		log "Skipping internal header ${base_name}"
		continue
	fi
	cp -f "$h" "${VENDORED_DIR}/"
	log "Copied ${base_name}"
done

# Ensure astl_test_hooks.h removed if previously vendored
if [[ -f "${VENDORED_DIR}/astl_test_hooks.h" ]]; then
	rm -f "${VENDORED_DIR}/astl_test_hooks.h"
	log "Removed previously vendored astl_test_hooks.h"
fi

# Copy generated version header
cp -f "${GEN_VERSION_H}" "${VENDORED_DIR}/astl_version.h"
log "Copied generated astl_version.h"

log "Vendored headers complete -> ${VENDORED_DIR}"
