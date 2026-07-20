#!/usr/bin/env bash

# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

# This script checks if the code is formatted according to clang-format rules.
# run this from the repo root
set -eu -o pipefail

# Check for dot (from the graphviz package)
if ! command -v dot >/dev/null 2>&1; then
	echo "❌ dot is not installed."
	echo "👉 Please install the graphviz package with:"
	echo "   sudo apt install graphviz   # Debian/Ubuntu"
	echo "   brew install graphviz       # macOS (Homebrew)"
	exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# set cwd to repo root
cd "${SCRIPT_DIR}"/..

cmake -S . -B build/graphviz --graphviz=build/graphviz/graph.dot >/dev/null
if diff build/graphviz/graph.dot doc/dependency_graph.dot >/dev/null; then
	echo "✅ No changes to the dependency graph!"
	exit 0
fi

cp build/graphviz/graph.dot doc/dependency_graph.dot
dot -Tsvg -o doc/dependency_graph.svg doc/dependency_graph.dot
echo "✨ docs/dependency_graph.svg ✨ has been updated!"
