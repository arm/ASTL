#!/usr/bin/env bash

# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

# Record that the repository's pre-commit framework hooks ran for this commit.

set -euo pipefail

if [[ $# -ne 1 ]]; then
	echo "Usage: $0 <commit-message-file>" >&2
	exit 2
fi

commit_message_file="$1"
if [[ ! -f $commit_message_file ]]; then
	echo "Error: commit message file does not exist: $commit_message_file" >&2
	exit 2
fi

git interpret-trailers \
	--in-place \
	--if-exists doNothing \
	--trailer "Pre-Commit-Ran: true" \
	"$commit_message_file"
