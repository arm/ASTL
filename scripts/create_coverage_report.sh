#!/usr/bin/env bash
set -euo pipefail

# Check for exactly one argument
if [ "$#" -ne 1 ]; then
    echo "Usage: $0 [--html | --xml]"
    exit 1
fi

output_args=""

# Validate the argument
case "$1" in
    --html)
        mkdir -p coverage
        output_args="--html-details -o coverage/index.html"
        ;;
    --xml)
        output_args="--xml -o coverage.xml"
        ;;
    *)
        echo "Invalid option: $1"
        echo "Usage: $0 [--html | --xml]"
        exit 1
        ;;
esac

# Check for gcovr
if ! command -v gcovr >/dev/null 2>&1; then
    echo "❌ gcovr is not installed."
    echo "👉 Please install it with:"
    echo "   sudo apt install gcovr        # Debian/Ubuntu"
    echo "   brew install gcovr            # macOS (Homebrew)"
    exit 1
fi

gcovr -r . \
    --exclude 'samples/*' \
    --exclude 'tests/*' \
    --exclude 'src/astl_test_hooks.cpp' \
    --exclude 'build/*' \
    --exclude-unreachable-branches \
    --exclude-throw-branches \
    $output_args

echo "✅ Coverage report generated successfully!"
if [[ "$1" == "--html" ]]; then
    echo "👉 Open coverage/index.html in your browser to view the report."
else
    echo "👉 Open coverage.xml to view the report."
fi