#!/usr/bin/env bash

# This script finds source files and runs clang-tidy to lint them
set -e

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <build>"
    echo "build path is needed to help clang-tidy avoid linting cmake-generated source files."
    exit 1
fi

# Check for clang-tidy
if ! command -v clang-tidy >/dev/null 2>&1; then
    echo "❌ clang-tidy is not installed."
    echo "👉 Please install it with:"
    echo "   sudo apt install clang-tidy        # Debian/Ubuntu"
    echo "   brew install llvm                                # macOS (Homebrew)"
    echo "   export PATH="/opt/homebrew/opt/llvm/bin:\$PATH"  # macOS continued"
    echo "   pacman -S clang                    # Arch"
    exit 1
fi

# check if jq exists
if ! command -v jq >/dev/null 2>&1; then
    echo "❌ jq is not installed."
    echo "👉 Please install it with:"
    echo "   sudo apt install jq                 # Debian/Ubuntu"
    echo "   brew install jq                     # macOS (Homebrew)"
    echo "   pacman -S jq                        # Arch"
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


# Include dependency headers from vcpkg as system headers
readarray -t VCPKG_DEPENDENCIES < <(jq -r '.dependencies[]' $REPO_ROOT_DIR/vcpkg.json)
if [ -f build/*/CMakeCache.txt ]; then
  TRIPLET=$(grep VCPKG_TARGET_TRIPLET $BUILD_DIR/CMakeCache.txt | head -n1 | cut -d '=' -f2)
  for dep in "${VCPKG_DEPENDENCIES[@]}"; do
    new_include="$REPO_ROOT_DIR/vcpkg/packages/${dep}_$TRIPLET/include"
    SYS_INCLUDE_PATHS=$SYS_INCLUDE_PATHS:" -isystem $new_include"
  done
else
  echo "No vcpkg triplet found, won't be able to provide dependencies as system headers"
  echo "Configure and maybe build a workspace first"
fi

# lint one translation unit at a time (assume headers are #included)
FILES=$(find $REPO_ROOT_DIR \( $PRUNE_EXPR \) -prune -o \( -type f \( $NAME_ALL_SOURCES \) \) -print)
for FILE in $FILES; do
  echo "- Linting $FILE"
  # exclude the C-level headers from this C++ lint - we'll do them as a separate step
  clang-tidy -header-filter='^(?!.*(include/astl|$BUILD_DIR/include/astl)).*' $FILE $CLANG_BUILD_DIR --warnings-as-errors=* -- $INCLUDE_PATHS $SYS_INCLUDE_PATHS

  # enable this to fix certain checks:
  # clang-tidy $CLANG_BUILD_DIR -checks=-*,readability-identifier-naming -fix -fix-errors -header-filter=. $FILE -- $INCLUDE_PATHS $SYS_INCLUDE_PATHS
done

# create a temporary C file to include all the C headers. This will cause clang-tidy to evaluate them as C headers
# (We had excluded them before to keep clang-tidy from trying to lint them as C++ headers)
echo "- Linting C Headers"
SRC_CODE='#include <astl/astl.h>\nint main() { return 0; }'
TEMP_C=$(mktemp --suffix=.c)
trap "rm -f '$TEMP_C'" EXIT
echo -e $SRC_CODE > $TEMP_C
# lint the dummy C file that includes ASTL API files
clang-tidy -header-filter='^(.*(include|$BUILD_DIR/include)).*' $TEMP_C $CLANG_BUILD_DIR --warnings-as-errors=* -- $INCLUDE_PATHS $SYS_INCLUDE_PATHS
