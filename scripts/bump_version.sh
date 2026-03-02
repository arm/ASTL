#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

# Check for exactly 2 parameters
if [ "$#" -ne 2 ]; then
	echo "Error: Exactly 2 parameters required" >&2
	echo "Usage: $0 <NONE|MAJOR|MINOR|PATCH> <version>" >&2
	exit 1
fi

# Validate first parameter
BUMP_TYPE="$1"
if [[ ! $BUMP_TYPE =~ ^(NONE|MAJOR|MINOR|PATCH)$ ]]; then
	echo "Error: First parameter must be NONE, MAJOR, MINOR, or PATCH" >&2
	exit 1
fi

# Get version and strip .post suffix if present for processing
VERSION="$2"
VERSION_CLEAN="${VERSION%.post}"

# Parse version numbers
IFS='.' read -r MAJOR MINOR PATCH <<<"$VERSION_CLEAN"

# Validate that we have 3 numbers
if [[ -z $MAJOR || -z $MINOR || -z $PATCH ]]; then
	echo "Error: Version must be in format X.Y.Z or X.Y.Z.post" >&2
	exit 1
fi

# Validate that components are numbers
if ! [[ $MAJOR =~ ^[0-9]+$ ]] || ! [[ $MINOR =~ ^[0-9]+$ ]] || ! [[ $PATCH =~ ^[0-9]+$ ]]; then
	echo "Error: Version components must be numeric" >&2
	exit 1
fi

# Perform version bump based on type
case "$BUMP_TYPE" in
NONE)
	echo "$MAJOR.$MINOR.$PATCH"
	;;
MAJOR)
	echo "$((MAJOR + 1)).0.0"
	;;
MINOR)
	echo "$MAJOR.$((MINOR + 1)).0"
	;;
PATCH)
	echo "$MAJOR.$MINOR.$((PATCH + 1))"
	;;
esac
