#!/usr/bin/env bash

# This script finds source files and runs cppcheck for a static analysis check
set -eu -o pipefail

if [ "$#" -ne 1 ]; then
	echo "Usage: $0 <build>"
	echo "build path is needed to help cppcheck avoid linting cmake-generated source files."
	exit 1
fi

# Check for clang-tidy
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
INCLUDE_PATHS=" -I${REPO_ROOT_DIR}/include"

# if the script was given an argument for the build output, add the include path from that, and exclude it from linting
if [[ -n $1 ]]; then
	BUILD_DIR=$(realpath "$1")

	INCLUDE_PATHS+=" -I${BUILD_DIR}/include"
	INCLUDE_PATHS+=" -I${REPO_ROOT_DIR}/utils"
	INCLUDE_PATHS+=" -I${REPO_ROOT_DIR}/src/impl"
	INCLUDE_PATHS+=" -I${REPO_ROOT_DIR}/src/impl/common"
fi

echo "Running cppcheck to lint code..."

# find the system header paths so cppcheck
SYS_INCLUDE_PATHS=$(echo | g++ -E -x c++ - -v 2>&1 |
	awk '/#include <...> search starts here:/{flag=1;next}/End of search list/{flag=0}flag' |
	sed 's/^/ -I /')

# Include dependency headers from vcpkg as system headers
for DEP in "$REPO_ROOT_DIR/external/vcpkg/packages"/*; do
	NEW_INCLUDE="${DEP}/include/"
	SYS_INCLUDE_PATHS+=" -system ${NEW_INCLUDE}"
done

#echo "cppcheck -U_WIN32 --inline-suppr --enable=all $REPO_ROOT_DIR/src/ $REPO_ROOT_DIR/tests/ $INCLUDE_PATHS --suppress=missingIncludeSystem"

FOLDERS=()
FOLDERS+=("$REPO_ROOT_DIR"/tools/)
FOLDERS+=("$REPO_ROOT_DIR"/samples/)
FOLDERS+=("$REPO_ROOT_DIR"/src/)
FOLDERS+=("$REPO_ROOT_DIR"/tests/)
FOLDERS+=("$REPO_ROOT_DIR"/utils/)

set -x

# suppress syntaxError since cppcheck 2.13 (on ubuntu-latest github runner) considers variadic macros with __VA_OPT__ an error
# suppress unknownMacro since cppcheck struggles to parse variadic macros with __VA_OPT__
cppcheck -U_WIN32 --std=c++23 --inline-suppr --enable=all "${FOLDERS[@]}" "$INCLUDE_PATHS" \
	--suppress=unusedFunction \
	--suppress=syntaxError \
	--suppress=unknownMacro \
	--suppress=unmatchedSuppression \
	--suppress=missingInclude \
	--suppress=missingIncludeSystem \
	--suppress=normalCheckLevelMaxBranches \
	--suppress=*:*external/* \
	--quiet \
	--error-exitcode=1
