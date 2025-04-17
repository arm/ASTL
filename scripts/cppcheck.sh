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

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_ROOT_DIR="$( dirname ${SCRIPT_DIR} )"
INCLUDE_PATHS="-I ${REPO_ROOT_DIR}/include"

# don't lint builld dir, or vcpkg dependencies
source $SCRIPT_DIR/get_find_file_expressions.sh # define PRUNE_EXPR

# if the script was given an argument for the build output, add the include path from that, and exclude it from linting
if [[ -n "$1" ]]; then
  BUILD_DIR=$(realpath "$1")

  INCLUDE_PATHS+=" -I ${BUILD_DIR}/include"
  INCLUDE_PATHS+=" -I ${REPO_ROOT_DIR}/src/impl"
fi

echo "Running cppcheck to lint code..."

# find the system header paths so cppcheck
SYS_INCLUDE_PATHS=$(echo | g++ -E -x c++ - -v 2>&1 | \
  awk '/#include <...> search starts here:/{flag=1;next}/End of search list/{flag=0}flag' | \
  sed 's/^/ -I /')


# Include dependency headers from vcpkg as system headers
readarray -t VCPKG_DEPENDENCIES < <(jq -r '.dependencies[]' $REPO_ROOT_DIR/vcpkg.json)
if [ -f build/*/CMakeCache.txt ]; then
  TRIPLET=$(grep VCPKG_TARGET_TRIPLET $BUILD_DIR/CMakeCache.txt | head -n1 | cut -d '=' -f2)
  for dep in "${VCPKG_DEPENDENCIES[@]}"; do
    new_include="$REPO_ROOT_DIR/vcpkg/packages/${dep}_$TRIPLET/include"
    SYS_INCLUDE_PATHS+=" -I $new_include"
  done
else
  echo "No vcpkg triplet found, won't be able to provide dependencies as system headers"
  echo "Configure and maybe build a workspace first"
fi

#echo "cppcheck --enable=all $REPO_ROOT_DIR/src/ $REPO_ROOT_DIR/tests/ $INCLUDE_PATHS $SYS_INCLUDE_PATHS"

cppcheck -U_WIN32 --enable=all $REPO_ROOT_DIR/src/ $REPO_ROOT_DIR/tests/ $INCLUDE_PATHS --suppress=missingIncludeSystem
