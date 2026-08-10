#!/usr/bin/env bash

# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

# Emit non-blocking GitHub Actions warnings for PR commits without the hook trailer.

set -euo pipefail

if [[ $# -ne 2 ]]; then
	echo "Usage: $0 <base-revision> <head-revision>" >&2
	exit 2
fi

base_revision="$1"
head_revision="$2"
missing_count=0

while IFS= read -r commit; do
	if ! git show --no-patch --format=%B "$commit" |
		git interpret-trailers --parse |
		grep -Fqx "Pre-Commit-Ran: true"; then
		short_commit="$(git rev-parse --short "$commit")"
		echo "::warning title=Pre-commit hook not detected::Commit ${short_commit} does not contain the pre-commit trailer"
		missing_count=$((missing_count + 1))
	fi
done < <(git rev-list --reverse --no-merges "${base_revision}..${head_revision}")

if [[ -n ${GITHUB_OUTPUT:-} ]]; then
	echo "missing_count=$missing_count" >>"$GITHUB_OUTPUT"
fi

if [[ -n ${GITHUB_STEP_SUMMARY:-} ]]; then
	{
		echo "## Pre-commit hook adoption"
		echo
		if [[ $missing_count -eq 0 ]]; then
			# Disable warnings about failing to expand `` since we don't want them expanded.
			# shellcheck disable=SC2016
			echo 'All non-merge commits in this pull request contain `Pre-Commit-Ran: true`.'
		else
			echo "Warning: ${missing_count} non-merge commit(s) do not contain \`Pre-Commit-Ran: true\`."
			echo
			# shellcheck disable=SC2016
			echo 'Run `./scripts/hooks/install_hooks.sh` to install the ASTL pre-commit hooks.'
		fi
	} >>"$GITHUB_STEP_SUMMARY"
fi

# Adoption monitoring is intentionally informational and must never block a pull request.
exit 0
