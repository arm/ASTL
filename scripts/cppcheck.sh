#!/usr/bin/env bash

# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

# This script finds source files and runs cppcheck for a static analysis check
set -eu -o pipefail

if [ "$#" -ne 1 ]; then
	echo "Usage: $0 <build>"
	echo "build path is needed to help cppcheck avoid linting cmake-generated source files."
	exit 1
fi

# Check for cppcheck
if ! command -v cppcheck >/dev/null 2>&1; then
	echo "❌ cppcheck is not installed."
	echo "👉 Please install it with:"
	echo "   sudo apt install cppcheck        # Debian/Ubuntu"
	echo "   brew install cppcheck       # macOS (Homebrew)"
	exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT_DIR="$(dirname "${SCRIPT_DIR}")"
echo "REPO_ROOT_DIR='$REPO_ROOT_DIR'"
declare -a EXTERNAL_SUPPRESSIONS=()
declare -a PLATFORM_DEFINES=()

BUILD_DIR=$(realpath "$1")
COMPILE_COMMANDS_FILE="${BUILD_DIR}/compile_commands.json"

if [[ ! -f ${COMPILE_COMMANDS_FILE} ]]; then
	echo "❌ compile_commands.json not found at '${COMPILE_COMMANDS_FILE}'."
	echo "👉 Configure the build with CMake export commands enabled, then rerun this script."
	exit 1
fi

if [[ -d "${BUILD_DIR}/vcpkg_installed" ]]; then
	EXTERNAL_SUPPRESSIONS+=("--suppress=*:${BUILD_DIR}/vcpkg_installed/*")
fi
if [[ -d "${BUILD_DIR}/src/impl/gen" ]]; then
	EXTERNAL_SUPPRESSIONS+=("--suppress=*:${BUILD_DIR}/src/impl/gen/*")
fi

case "$(uname -s)" in
Linux*)
	PLATFORM_DEFINES+=("-D__linux__" "-D__unix__" "-U_WIN32")
	;;
Darwin*)
	PLATFORM_DEFINES+=("-D__APPLE__" "-D__unix__" "-U_WIN32")
	;;
MINGW* | MSYS* | CYGWIN*)
	PLATFORM_DEFINES+=("-D_WIN32")
	;;
esac

echo "Running cppcheck to lint code..."

# suppress syntaxError since cppcheck 2.13 (on ubuntu-latest github runner) considers variadic macros with __VA_OPT__ an error
# suppress unknownMacro since cppcheck struggles to parse variadic macros with __VA_OPT__
cppcheck -v --project="${COMPILE_COMMANDS_FILE}" "${PLATFORM_DEFINES[@]}" --std=c++23 --inline-suppr --enable=all \
	--suppress=unusedFunction \
	--suppress=syntaxError \
	--suppress=unknownMacro \
	--suppress=unmatchedSuppression \
	--suppress=missingInclude \
	--suppress=missingIncludeSystem \
	--suppress=normalCheckLevelMaxBranches \
	--suppress=*:*external/* \
	"${EXTERNAL_SUPPRESSIONS[@]}" \
	--quiet \
	--error-exitcode=1
