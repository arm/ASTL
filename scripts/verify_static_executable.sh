#!/usr/bin/env bash

# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

if [[ $# -ne 1 ]]; then
	echo "Usage: $0 <executable>" >&2
	exit 2
fi

executable="$1"
if [[ ! -x $executable ]]; then
	echo "Error: executable does not exist or is not executable: $executable" >&2
	exit 1
fi

for required_tool in file readelf; do
	if ! command -v "$required_tool" >/dev/null 2>&1; then
		echo "Error: required tool is unavailable: $required_tool" >&2
		exit 1
	fi
done

file_output="$(file "$executable")"
echo "$file_output"
if [[ $file_output != *"statically linked"* && $file_output != *"static-pie linked"* ]]; then
	echo "Error: file did not identify $executable as statically linked" >&2
	exit 1
fi

if readelf --program-headers --wide "$executable" | grep -qE '^[[:space:]]*INTERP'; then
	echo "Error: $executable contains an ELF interpreter and is dynamically linked" >&2
	exit 1
fi

if readelf --dynamic --wide "$executable" | grep -q '(NEEDED)'; then
	echo "Error: $executable contains dynamic library dependencies" >&2
	readelf --dynamic --wide "$executable" >&2
	exit 1
fi

echo "Verified fully static executable: $executable"
