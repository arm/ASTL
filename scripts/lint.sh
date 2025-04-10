#!/usr/bin/env bash

# This script finds source files and runs clang-tidy to lint them
set -e

# Check for clang-tidy
if ! command -v clang-tidy >/dev/null 2>&1; then
    echo "❌ clang-tidy is not installed."
    echo "👉 Please install it with:"
    echo "   sudo apt install clang-tidy          # Debian/Ubuntu"
    echo "   brew install clang-tidy              # macOS (Homebrew)"
    echo "   export PATH="/opt/homebrew/opt/llvm/bin:\$PATH"  # macOS continued"
    exit 1
fi

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_ROOT_DIR="$( dirname ${SCRIPT_DIR} )"
INCLUDE_PATHS="-I${REPO_ROOT_DIR}/include"

# don't lint builld dir, or vcpkg dependencies
source $SCRIPT_DIR/get_find_file_expressions.sh # define PRUNE_EXPR

CLANG_BUILD_DIR=""
# if the script was given an argument for the build output, add the include path from that, and exclude it from linting
if [[ -n "$1" ]]; then
  BUILD_DIR=$(realpath "$1")

  INCLUDE_PATHS+=" -I${BUILD_DIR}/include"
  INCLUDE_PATHS+=" -I${REPO_ROOT_DIR}/src/impl"
  CLANG_BUILD_DIR+=" -p $BUILD_DIR"
fi

echo "Running clang-tidy to lint code..."

# find the system header paths so clang++ can find them
SYS_INCLUDE_PATHS=$(echo | g++ -E -x c++ - -v 2>&1 | \
  awk '/#include <...> search starts here:/{flag=1;next}/End of search list/{flag=0}flag' | \
  sed 's/^/ -isystem /')

# lint one translation unit at a time (assume headers are #included)
FILES=$(find $REPO_ROOT_DIR \( $PRUNE_EXPR \) -prune -o \( -type f \( $NAME_ALL_SOURCES \) \) -print)
for FILE in $FILES; do
   echo "- Linting $FILE"
   clang-tidy $CLANG_BUILD_DIR -header-filter=. $FILE -- $INCLUDE_PATHS $SYS_INCLUDE_PATHS
   # enable this to fix certain checks:
   # clang-tidy $CLANG_BUILD_DIR -checks=-*,readability-identifier-naming -fix -fix-errors -header-filter=. $FILE -- $INCLUDE_PATHS $SYS_INCLUDE_PATHS
done
