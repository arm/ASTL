#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TEST_ROOT="$(mktemp -d)"
trap 'rm -rf "${TEST_ROOT}"' EXIT
mkdir -p "${TEST_ROOT}/bin"
cat >"${TEST_ROOT}/bin/gh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
args="$*"
if [[ ${args} == *"--method POST"* && ${args} == *"/check-runs"* ]]; then jq -n '{id: 4242}'; exit 0; fi
if [[ ${args} == *"--method POST"* && ${args} == *"/dispatches"* ]]; then cat >"${FAKE_GH_PAYLOAD}"; exit "${FAKE_DISPATCH_STATUS:-0}"; fi
if [[ ${args} == *"--method PATCH"* ]]; then printf '%s' "${args}" >"${FAKE_GH_PATCH}"; exit 0; fi
echo "Unexpected fake gh invocation: ${args}" >&2; exit 3
EOF
chmod +x "${TEST_ROOT}/bin/gh"
export PATH="${TEST_ROOT}/bin:${PATH}" GH_TOKEN=private PUBLIC_GH_TOKEN=public
export FAKE_GH_PAYLOAD="${TEST_ROOT}/payload.json" FAKE_GH_PATCH="${TEST_ROOT}/patch.txt"
export PUBLIC_RUN_URL=https://github.com/Arm-Debug/ASTL/actions/runs/9001
export SOURCE_EVENT=pull_request TARGET_REPOSITORY=Arm-Debug/ASTL HEAD_REPOSITORY=contributor/ASTL
export HEAD_SHA=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA BASE_SHA=BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB
export PR_NUMBER=88 CORRELATION_ID=public-pr-88-9001-2
"${ROOT}/scripts/amx_ci_gate.sh"
jq -e '.client_payload.schema_version == 2 and .client_payload.callback.check_run_id == "4242" and
 .client_payload.callback.event_type == "amx-ci-result" and .client_payload.head_sha == "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"' "${FAKE_GH_PAYLOAD}" >/dev/null
export SOURCE_EVENT=push HEAD_REPOSITORY=Arm-Debug/ASTL
export HEAD_SHA=CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC BASE_SHA=DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD
export PR_NUMBER=0 CORRELATION_ID=public-main-9002-1
"${ROOT}/scripts/amx_ci_gate.sh"
jq -e '.client_payload.source_event == "push" and .client_payload.pr_number == "0"' "${FAKE_GH_PAYLOAD}" >/dev/null
export FAKE_DISPATCH_STATUS=1
if "${ROOT}/scripts/amx_ci_gate.sh" >/dev/null 2>&1; then
	echo "Accepted failed dispatch" >&2
	exit 1
fi
grep -Fq 'conclusion=failure' "${FAKE_GH_PATCH}"
echo "PASS AMX confidential CI gate tests"
