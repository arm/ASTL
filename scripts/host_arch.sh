#!/usr/bin/env bash

# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

machine="${1:-$(uname -m)}"
machine="$(printf '%s' "${machine}" | tr '[:upper:]' '[:lower:]')"

case "${machine}" in
aarch64 | arm64)
	printf 'arm64\n'
	;;
amd64 | x64 | x86_64)
	printf 'x86_64\n'
	;;
i386 | i486 | i586 | i686 | x86)
	printf 'x86\n'
	;;
*)
	printf '%s\n' "$(printf '%s' "${machine}" | tr -c '[:alnum:]_+-' '_')"
	;;
esac
