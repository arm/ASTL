#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

# Install git hooks for auto-formatting and license linting

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
HOOKS_DIR="$REPO_ROOT/.git/hooks"
PRE_COMMIT_HOOK_FILE="$HOOKS_DIR/pre-commit"

echo "Installing git hooks..."

# Check if .git directory exists
if [ ! -d "$REPO_ROOT/.git" ]; then
	echo "Error: Not a git repository. Run this from within the ASTL repository."
	exit 1
fi

# Create hooks directory if it doesn't exist
mkdir -p "$HOOKS_DIR"

# Create the pre-commit hook
cat >"$PRE_COMMIT_HOOK_FILE" <<'EOF'
#!/bin/bash
# Git hook to auto-format staged files and run license lint checks before commit

set -euo pipefail

HOOKS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$HOOKS_DIR/../.." && pwd)"
FORMAT_SCRIPT="$REPO_ROOT/scripts/format.sh"
LICENSE_LINT_SCRIPT="$REPO_ROOT/scripts/license_lint.sh"

if [ ! -x "$FORMAT_SCRIPT" ]; then
	echo "❌ Missing executable format script: $FORMAT_SCRIPT"
	exit 1
fi

if [ ! -x "$LICENSE_LINT_SCRIPT" ]; then
	echo "❌ Missing executable license lint script: $LICENSE_LINT_SCRIPT"
	exit 1
fi

STAGED_FILES=()
while IFS= read -r file; do
	STAGED_FILES+=("$file")
done < <(git diff --cached --name-only --diff-filter=ACMR)

if [ "${#STAGED_FILES[@]}" -gt 0 ]; then
	echo "Running formatter before commit"
	"$FORMAT_SCRIPT"

	echo "Re-staging formatted files"
	for file in "${STAGED_FILES[@]}"; do
		if [ -e "$REPO_ROOT/$file" ]; then
			git add -- "$file"
		fi
	done
fi

echo "Running license lint before commit"

"$LICENSE_LINT_SCRIPT"
EOF

# Make the hooks executable
chmod +x "$PRE_COMMIT_HOOK_FILE"

echo "✓ Auto-format and license pre-commit hook installed at: $PRE_COMMIT_HOOK_FILE"
echo ""
echo "The pre-commit hook will run scripts/format.sh, re-stage any staged files it changed,"
echo "and then run scripts/license_lint.sh before each commit."
echo ""
echo "To uninstall:"
echo "  rm $PRE_COMMIT_HOOK_FILE"
