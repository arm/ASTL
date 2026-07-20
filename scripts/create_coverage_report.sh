#!/usr/bin/env bash

# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

# Check for exactly one argument
if [ "$#" -ne 1 ]; then
	echo "Usage: $0 [--html | --xml]" >/dev/stderr
	exit 1
fi

output_args=""

# Validate the argument
case "$1" in
--html)
	mkdir -p coverage
	output_args=(--html-details -o coverage/index.html)
	;;
--xml)
	output_args=(--xml -o coverage.xml)
	;;
*)
	echo "Invalid option: $1" >/dev/stderr
	echo "Usage: $0 [--html | --xml]" >/dev/stderr
	exit 1
	;;
esac

# Check for gcovr
if ! command -v gcovr >/dev/null 2>&1; then
	echo "❌ gcovr is not installed." >/dev/stderr
	echo "👉 Please install it with:" >/dev/stderr
	echo "   sudo apt install gcovr        # Debian/Ubuntu" >/dev/stderr
	echo "   brew install gcovr            # macOS (Homebrew)" >/dev/stderr
	exit 1
fi

gcovr -r . --verbose \
	--exclude 'samples/*' \
	--exclude 'tests/*' \
	--exclude 'src/astl_test_hooks.cpp' \
	--exclude 'build/*' \
	--exclude 'third_party/*' \
	--exclude 'tools/ATX' \
	--exclude 'tools/mock_scmi' \
	--exclude-unreachable-branches \
	--exclude-throw-branches \
	"${output_args[@]}"

echo "✅ Coverage report generated successfully!"
if [[ $1 == "--html" ]]; then
	echo "👉 Open coverage/index.html in your browser to view the report."
else
	echo "👉 Open coverage.xml to view the report."
fi
