#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
# SPDX-License-Identifier: Apache-2.0
# shellcheck disable=SC2153  # Environment names are validated indirectly below.

set -euo pipefail

readonly PRIVATE_REPOSITORY="Arm-Debug/ASTL-confidential"
readonly PRIVATE_WORKFLOW="amx-ci.yml"
readonly POLL_INTERVAL_SECONDS="${AMX_CI_POLL_INTERVAL_SECONDS:-15}"
readonly MAX_START_POLLS="${AMX_CI_MAX_START_POLLS:-40}"
readonly MAX_COMPLETION_POLLS="${AMX_CI_MAX_COMPLETION_POLLS:-720}"

required=(
	GH_TOKEN
	SOURCE_EVENT
	TARGET_REPOSITORY
	HEAD_REPOSITORY
	HEAD_SHA
	BASE_SHA
	PR_NUMBER
	CORRELATION_ID
)
for name in "${required[@]}"; do
	if [[ -z ${!name:-} ]]; then
		echo "Required environment variable is empty: ${name}" >&2
		exit 2
	fi
done

source_event="${SOURCE_EVENT}"
target_repository="${TARGET_REPOSITORY}"
head_repository="${HEAD_REPOSITORY}"
head_sha="${HEAD_SHA,,}"
base_sha="${BASE_SHA,,}"
pr_number="${PR_NUMBER}"
correlation_id="${CORRELATION_ID}"

if [[ ! ${POLL_INTERVAL_SECONDS} =~ ^[0-9]+$ ||
	! ${MAX_START_POLLS} =~ ^[1-9][0-9]*$ ||
	! ${MAX_COMPLETION_POLLS} =~ ^[1-9][0-9]*$ ]]; then
	echo "AMX CI polling controls must be non-negative integers with positive limits." >&2
	exit 2
fi
if [[ ! ${source_event} =~ ^(pull_request|push)$ ]]; then
	echo "Unsupported source event: ${source_event}" >&2
	exit 2
fi
if [[ ${target_repository} != "Arm-Debug/ASTL" ]]; then
	echo "Unsupported target repository: ${target_repository}" >&2
	exit 2
fi
if [[ ! ${head_repository} =~ ^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$ ]]; then
	echo "Invalid head repository: ${head_repository}" >&2
	exit 2
fi
if [[ ! ${head_sha} =~ ^[0-9a-f]{40}$ || ! ${base_sha} =~ ^[0-9a-f]{40}$ ]]; then
	echo "Head and base revisions must be full commit SHAs." >&2
	exit 2
fi
if [[ ! ${pr_number} =~ ^[0-9]+$ ]]; then
	echo "Invalid pull request number: ${pr_number}" >&2
	exit 2
fi
if [[ ${source_event} == "pull_request" && ${pr_number} == "0" ]]; then
	echo "Pull-request AMX CI requires a non-zero pull request number." >&2
	exit 2
fi
if [[ ${source_event} == "push" &&
	(${pr_number} != "0" || ${head_repository} != "${target_repository}") ]]; then
	echo "Push AMX CI requires pr_number=0 and matching target/head repositories." >&2
	exit 2
fi
if [[ ! ${correlation_id} =~ ^[A-Za-z0-9_.-]+$ || ${#correlation_id} -gt 180 ]]; then
	echo "Invalid AMX CI correlation ID." >&2
	exit 2
fi

payload="$(jq -n \
	--arg source_event "${source_event}" \
	--arg target_repository "${target_repository}" \
	--arg head_repository "${head_repository}" \
	--arg head_sha "${head_sha}" \
	--arg base_sha "${base_sha}" \
	--arg pr_number "${pr_number}" \
	--arg correlation_id "${correlation_id}" \
	'{
      event_type: "amx-ci-public-change",
      client_payload: {
        schema_version: 1,
        source_event: $source_event,
        target_repository: $target_repository,
        head_repository: $head_repository,
        head_sha: $head_sha,
        base_sha: $base_sha,
        pr_number: $pr_number,
        correlation_id: $correlation_id
      }
    }')"

echo "Dispatching AMX CI correlation ${correlation_id}."
gh api \
	--method POST \
	-H "Accept: application/vnd.github+json" \
	"repos/${PRIVATE_REPOSITORY}/dispatches" \
	--input - <<<"${payload}"

run_id=""
for ((poll = 1; poll <= MAX_START_POLLS; poll++)); do
	runs="$(gh api \
		-H "Accept: application/vnd.github+json" \
		"repos/${PRIVATE_REPOSITORY}/actions/workflows/${PRIVATE_WORKFLOW}/runs?event=repository_dispatch&per_page=100")"
	run_id="$(jq -r \
		--arg title "AMX CI ${correlation_id}" \
		'.workflow_runs[] | select(.display_title == $title) | .id' \
		<<<"${runs}" | head -n 1)"
	if [[ -n ${run_id} ]]; then
		break
	fi
	sleep "${POLL_INTERVAL_SECONDS}"
done

if [[ -z ${run_id} ]]; then
	echo "Timed out waiting for the AMX workflow to start." >&2
	exit 1
fi

details_url=""
for ((poll = 1; poll <= MAX_COMPLETION_POLLS; poll++)); do
	run="$(gh api \
		-H "Accept: application/vnd.github+json" \
		"repos/${PRIVATE_REPOSITORY}/actions/runs/${run_id}")"
	status="$(jq -r '.status' <<<"${run}")"
	conclusion="$(jq -r '.conclusion // ""' <<<"${run}")"
	details_url="$(jq -r '.html_url' <<<"${run}")"
	if [[ ${status} == "completed" ]]; then
		{
			echo "### AMX CI"
			echo "- Correlation: \`${correlation_id}\`"
			echo "- Private result: ${conclusion}"
			echo "- [Internal workflow details](${details_url})"
		} >>"${GITHUB_STEP_SUMMARY}"
		if [[ ${conclusion} == "success" ]]; then
			exit 0
		fi
		echo "AMX CI concluded with: ${conclusion}" >&2
		exit 1
	fi
	sleep "${POLL_INTERVAL_SECONDS}"
done

echo "Timed out waiting for AMX CI to complete: ${details_url}" >&2
exit 1
