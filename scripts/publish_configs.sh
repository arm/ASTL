#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

set -eu -o pipefail

# Script to publish ASTL config directory with filtering
# This script copies the config/ directory with the following modifications:
# 1. Filters out confidential content based on --confidential flag
# 2. Excludes JSON files with document.confidential == true
# 3. Removes JSON elements within files that have "confidential": true
#
# @TODO(ASTL-348) Properly render alias_table.json to filter out irrelevent/confidential IPs
# @TODO(ASTL-351) Consider tradeoffs of .toml vs .json for the alias table

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Default values
OUTPUT_DIR=""
INCLUDE_CONFIDENTIAL=false
INCLUDE_MOCKSYSFS=false

# Usage function
usage() {
	cat <<EOF
Usage: $0 -o OUTPUT_DIR [OPTIONS]

Publish ASTL config directory with filtering and SCMI spec version selection.

Required arguments:
  -o OUTPUT_DIR             Output directory to copy config/ to (with modifications)

Optional arguments:
  --confidential            Include confidential content (default: exclude confidential content)
  --mocksysfs               Include mock scmi sysfs metrics and scmi spec for testing
  -h, --help                Show this help message

Examples:
  # Publish spec for non-confidential/public use
  $0 -o /path/to/output

  # Publish spec including confidential content and mocksysfs targets
  $0 -o /path/to/output --confidential --mocksysfs

EOF
	exit 1
}

# Parse command-line arguments
while [[ $# -gt 0 ]]; do
	case $1 in
	-o)
		OUTPUT_DIR="$2"
		shift 2
		;;
	--confidential)
		INCLUDE_CONFIDENTIAL=true
		shift
		;;
	--mocksysfs)
		INCLUDE_MOCKSYSFS=true
		shift
		;;
	-h | --help)
		usage
		;;
	*)
		echo "Error: Unknown option: $1" >&2
		usage
		;;
	esac
done

# Validate required arguments
if [[ -z $OUTPUT_DIR ]]; then
	echo "Error: Output directory (-o) is required" >&2
	usage
fi

# Check if jq is available for JSON processing
if ! command -v jq >/dev/null 2>&1; then
	echo "Error: jq is required but not installed. Please install jq." >&2
	exit 1
fi

# Check if source config directory exists
CONFIG_DIR="$REPO_ROOT/config"
if [[ ! -d $CONFIG_DIR ]]; then
	echo "Error: Source config directory not found: $CONFIG_DIR" >&2
	exit 1
fi

# Function to copy and filter JSON files from a source directory to output
# Arguments:
#   $1: source directory
#   $2: output directory
#   $3: display name for logging
copy_and_filter_json_files() {
	local source_dir="$1"
	local output_dir="$2"
	local display_name="$3"

	if [[ ! -d $source_dir ]]; then
		echo "Warning: Source directory not found: $source_dir" >&2
		return
	fi

	echo "Copying and filtering $display_name files..."

	# Process JSON files
	find "$source_dir" -type f -name "*.json" | while IFS= read -r json_file; do
		# Get relative path from source_dir
		rel_path="${json_file#"$source_dir"/}"
		output_file="$output_dir/$rel_path"

		# Check if file has document.confidential == true (exclude entire file)
		if jq -e '.document.confidential == true' "$json_file" >/dev/null 2>&1; then
			if [[ $INCLUDE_CONFIDENTIAL == false ]]; then
				echo "  Excluding (document.confidential=true): $rel_path"
				continue
			fi
		fi

		# Check if file has top-level "confidential": true (exclude entire file)
		if jq -e 'has("confidential") and .confidential == true' "$json_file" >/dev/null 2>&1; then
			if [[ $INCLUDE_CONFIDENTIAL == false ]]; then
				echo "  Excluding (top-level confidential=true): $rel_path"
				continue
			fi
		fi

		# Create output directory for this file
		mkdir -p "$(dirname "$output_file")"

		if [[ $INCLUDE_CONFIDENTIAL == true ]]; then
			# For confidential distribution, copy as-is
			cp "$json_file" "$output_file"
			echo "  Copied (confidential): $rel_path"
		else
			# For non-confidential distribution, filter out confidential elements
			echo "  Filtering (removing confidential elements): $rel_path"

			# Recursively remove all objects/elements with "confidential": true
			# Keep objects with "confidential": false
			jq 'walk(
                if type == "object" then
                    if has("confidential") and .confidential == true then
                        null
                    else
                        with_entries(select(.value != null))
                    end
                elif type == "array" then
                    map(select(. != null))
                else
                    .
                end
            )' "$json_file" >"$output_file"

			# Verify the output is valid JSON
			if ! jq empty "$output_file" 2>/dev/null; then
				echo "  Warning: Failed to create valid JSON for $rel_path, copying original" >&2
				cp "$json_file" "$output_file"
			fi
		fi
	done

	# Copy non-JSON files
	find "$source_dir" -type f ! -name "*.json" | while IFS= read -r file; do
		rel_path="${file#"$source_dir"/}"
		output_file="$output_dir/$rel_path"
		mkdir -p "$(dirname "$output_file")"
		cp "$file" "$output_file"
		echo "  Copied (non-JSON): $rel_path"
	done
}

# Create output directory
echo "Creating output directory: $OUTPUT_DIR"
mkdir -p "$OUTPUT_DIR"

# Copy .gitignore if it exists
if [[ -f "$CONFIG_DIR/.gitignore" ]]; then
	cp "$CONFIG_DIR/.gitignore" "$OUTPUT_DIR/"
fi

# Copy and filter metrics directory
if [[ -d "$CONFIG_DIR/metrics" ]]; then
	copy_and_filter_json_files "$CONFIG_DIR/metrics" "$OUTPUT_DIR/metrics" "metrics"
fi

# Set up SCMI directory structure
echo ""
echo "Setting up SCMI directory structure"
SCMI_SOURCE="$CONFIG_DIR/scmi/public"
SCMI_OUTPUT="$OUTPUT_DIR/scmi/public"

if [[ ! -d $SCMI_SOURCE ]]; then
	echo "Error: SCMI source directory not found: $SCMI_SOURCE" >&2
	exit 1
fi

mkdir -p "$SCMI_OUTPUT"

# Copy and filter SCMI files
copy_and_filter_json_files "$SCMI_SOURCE" "$SCMI_OUTPUT" "SCMI"

if [[ $INCLUDE_MOCKSYSFS == true ]]; then
	echo ""
	echo "Including MockSysfs SCMI metrics and spec for testing..."

	MOCKSYSFS_SCMI_SPEC_SOURCE="$CONFIG_DIR/scmi/mocksysfs/mocksysfs"
	MOCKSYSFS_SCMI_SPEC_OUTPUT="$OUTPUT_DIR/scmi/public/mocksysfs"

	if [[ ! -d $MOCKSYSFS_SCMI_SPEC_SOURCE ]]; then
		echo "Error: MockSysfs source directory not found: $MOCKSYSFS_SCMI_SPEC_SOURCE" >&2
		exit 1
	fi

	mkdir -p "$MOCKSYSFS_SCMI_SPEC_OUTPUT"
	# Copy and filter MockSysfs SCMI files
	copy_and_filter_json_files "$MOCKSYSFS_SCMI_SPEC_SOURCE" "$MOCKSYSFS_SCMI_SPEC_OUTPUT" "MockSysfs SCMI"

	# merge the mocksysfs uuid_mapping with the scmi spec repometa.json uuid_mapping
	REPO_META_SOURCE="$CONFIG_DIR/scmi/mocksysfs/repometa.json"
	REPO_META_OUTPUT="$OUTPUT_DIR/scmi/public/repometa.json"
	if [[ -f $REPO_META_SOURCE ]]; then
		echo "Merging uuid_mapping from MockSysfs repometa.json..."
		jq -s '.[0] * {uuid_mapping: (.[0].uuid_mapping + .[1].uuid_mapping)}' \
			"$OUTPUT_DIR/scmi/public/repometa.json" "$REPO_META_SOURCE" >"$REPO_META_OUTPUT.tmp"
		mv "$REPO_META_OUTPUT.tmp" "$REPO_META_OUTPUT"
	else
		echo "Warning: MockSysfs repometa.json not found: $REPO_META_SOURCE" >&2
	fi
fi

echo ""
echo "Publishing complete!"
echo "Output directory: $OUTPUT_DIR"
echo "Confidential content: $([ "$INCLUDE_CONFIDENTIAL" == true ] && echo "included" || echo "excluded")"
