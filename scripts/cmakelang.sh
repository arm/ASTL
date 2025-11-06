#!/usr/bin/env bash

set -eu -o pipefail

if ! command -v cmake-lint >/dev/null 2>&1; then
	echo "❌ cmake-lint is not installed."
	echo "👉 Please install cmakelang (provides cmake-lint and cmake-format):"
	echo "   python3 -m pip install --user cmakelang"
	exit 1
fi

SCRIPT_DIR="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"
REPO_ROOT_DIR="$(dirname "${SCRIPT_DIR}")"
echo "REPO_ROOT_DIR='${REPO_ROOT_DIR}'"

echo "🧹 Linting CMake files with cmake-lint"

# Collect paths to lint
CMAKE_PATHS=(
	"${REPO_ROOT_DIR}/CMakeLists.txt"
	"${REPO_ROOT_DIR}/src"
	"${REPO_ROOT_DIR}/samples"
	"${REPO_ROOT_DIR}/tools"
	"${REPO_ROOT_DIR}/tests"
)

find "${CMAKE_PATHS[@]}" -type f -name 'CMakeLists.txt' | parallel -v cmake-lint --suppress-decorations -l error {} ||
	{
		echo "❌ cmake-lint found issues"
		exit 1
	}

echo "✅ All CMakeLists.txt files passed lint"
