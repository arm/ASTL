#!/usr/bin/env bash
# Install the Coverity URL commit-msg hook

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
HOOKS_DIR="$REPO_ROOT/.git/hooks"
HOOK_FILE="$HOOKS_DIR/commit-msg"

echo "Installing Coverity URL commit-msg hook..."

# Check if .git directory exists
if [ ! -d "$REPO_ROOT/.git" ]; then
	echo "Error: Not a git repository. Run this from within the ASTL repository."
	exit 1
fi

# Create hooks directory if it doesn't exist
mkdir -p "$HOOKS_DIR"

# Create the commit-msg hook

cat >"$HOOK_FILE" <<'EOF'
#!/bin/bash
# Git hook to automatically add Coverity query URLs to commit messages
# This hook runs AFTER the user has written the commit message

COMMIT_MSG_FILE=$1

# Get the directory of this script (should be .git/hooks)
HOOKS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$HOOKS_DIR/../.." && pwd)"
SCRIPT="$REPO_ROOT/scripts/hooks/add_coverity_url.py"

# Run the script if it exists
if [ -f "$SCRIPT" ]; then
    python3 "$SCRIPT" "$COMMIT_MSG_FILE"
fi
EOF

# Make the hook executable
chmod +x "$HOOK_FILE"

echo "✓ Coverity URL hook installed at: $HOOK_FILE"
echo ""
echo "The hook will automatically add Coverity query URLs when you mention CIDs in commit messages."
echo ""
echo "Example commit message:"
echo "  [ASTL-123] Fix coverity issues"
echo "  "
echo "  CID:"
echo "  123456"
echo "  789012"
echo ""
echo "Will automatically become:"
echo "  [ASTL-123] Fix coverity issues"
echo "  "
echo "  CID:"
echo "  123456"
echo "  789012"
echo "  [View in Coverity](https://coverity.geo.arm.com/query/defects.htm?cid=123456&cid=789012&stream=ASTL-main)"
echo ""
echo "To uninstall: rm $HOOK_FILE"
