#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
readonly ROOT
TEST_ROOT="$(mktemp -d)"
readonly TEST_ROOT
trap 'rm -rf "${TEST_ROOT}"' EXIT

mkdir -p "${TEST_ROOT}/bin"
cat >"${TEST_ROOT}/bin/gh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

arguments="$*"
if [[ ${arguments} == *"--method POST"* ]]; then
	cat >"${FAKE_GH_PAYLOAD}"
	exit 0
fi
if [[ ${arguments} == *"/actions/workflows/amx-ci.yml/runs?"* ]]; then
	jq -n --arg title "${FAKE_GH_DISPLAY_TITLE}" \
		'{workflow_runs: [{id: 4242, display_title: $title}]}'
	exit 0
fi
if [[ ${arguments} == *"/actions/runs/4242"* ]]; then
	jq -n --arg conclusion "${FAKE_GH_CONCLUSION}" \
		'{status: "completed", conclusion: $conclusion, html_url: "https://example.invalid/amx-ci/4242"}'
	exit 0
fi
echo "Unexpected fake gh invocation: ${arguments}" >&2
exit 3
EOF
chmod +x "${TEST_ROOT}/bin/gh"

export PATH="${TEST_ROOT}/bin:${PATH}"
export GH_TOKEN="test-token"
export AMX_CI_POLL_INTERVAL_SECONDS=0
export AMX_CI_MAX_START_POLLS=1
export AMX_CI_MAX_COMPLETION_POLLS=1
export FAKE_GH_PAYLOAD="${TEST_ROOT}/payload.json"
export GITHUB_STEP_SUMMARY="${TEST_ROOT}/summary.md"
export FAKE_GH_CONCLUSION=success

run_gate() {
	export FAKE_GH_DISPLAY_TITLE="AMX CI ${CORRELATION_ID}"
	: >"${GITHUB_STEP_SUMMARY}"
	"${ROOT}/scripts/amx_ci_gate.sh"
}

export SOURCE_EVENT=pull_request
export TARGET_REPOSITORY=Arm-Debug/ASTL
export HEAD_REPOSITORY=contributor/ASTL
HEAD_SHA="$(printf 'A%.0s' {1..40})"
BASE_SHA="$(printf 'B%.0s' {1..40})"
export HEAD_SHA BASE_SHA
export PR_NUMBER=88
export CORRELATION_ID=public-pr-88-9001-2
run_gate

jq -e '
  .event_type == "amx-ci-public-change" and
  .client_payload.schema_version == 1 and
  .client_payload.source_event == "pull_request" and
  .client_payload.target_repository == "Arm-Debug/ASTL" and
  .client_payload.head_repository == "contributor/ASTL" and
  .client_payload.head_sha == "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" and
  .client_payload.base_sha == "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb" and
  .client_payload.pr_number == "88" and
  .client_payload.correlation_id == "public-pr-88-9001-2"
' "${FAKE_GH_PAYLOAD}" >/dev/null
grep -Fq 'Private result: success' "${GITHUB_STEP_SUMMARY}"

export SOURCE_EVENT=push
export HEAD_REPOSITORY=Arm-Debug/ASTL
HEAD_SHA="$(printf 'C%.0s' {1..40})"
BASE_SHA="$(printf 'D%.0s' {1..40})"
export HEAD_SHA BASE_SHA
export PR_NUMBER=0
export CORRELATION_ID=public-main-9002-1
run_gate
jq -e '
  .client_payload.source_event == "push" and
  .client_payload.head_repository == "Arm-Debug/ASTL" and
  .client_payload.pr_number == "0"
' "${FAKE_GH_PAYLOAD}" >/dev/null

export FAKE_GH_CONCLUSION=failure
if run_gate >"${TEST_ROOT}/failure.stdout" 2>"${TEST_ROOT}/failure.stderr"; then
	echo "AMX CI gate accepted a failed private workflow." >&2
	exit 1
fi
grep -Fq 'AMX CI concluded with: failure' "${TEST_ROOT}/failure.stderr"
grep -Fq 'Private result: failure' "${GITHUB_STEP_SUMMARY}"
export FAKE_GH_CONCLUSION=success

export TARGET_REPOSITORY=Arm/ASTL
if run_gate >"${TEST_ROOT}/mirror.stdout" 2>"${TEST_ROOT}/mirror.stderr"; then
	echo "AMX CI gate accepted a mirrored target repository." >&2
	exit 1
fi
grep -Fq 'Unsupported target repository' "${TEST_ROOT}/mirror.stderr"
export TARGET_REPOSITORY=Arm-Debug/ASTL

export SOURCE_EVENT=unsupported
if "${ROOT}/scripts/amx_ci_gate.sh" >"${TEST_ROOT}/invalid.stdout" 2>"${TEST_ROOT}/invalid.stderr"; then
	echo "AMX CI gate accepted an unsupported source event." >&2
	exit 1
fi
grep -Fq 'Unsupported source event' "${TEST_ROOT}/invalid.stderr"

echo "PASS AMX confidential CI gate tests"
