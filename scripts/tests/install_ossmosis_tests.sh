#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)"
INSTALLER="${ROOT}/scripts/install_ossmosis.sh"
TEST_ROOT="$(mktemp -d)"
trap 'rm -rf "${TEST_ROOT}"' EXIT

fake_ossmosis="${TEST_ROOT}/ossmosis"
cat >"${fake_ossmosis}" <<'EOF'
#!/usr/bin/env bash
[[ ${1:-} == materialize && ${2:-} == --help ]]
EOF
chmod +x "${fake_ossmosis}"

resolved="$(OSSMOSIS_BIN="${fake_ossmosis}" "${INSTALLER}")"
[[ ${resolved} == "${fake_ossmosis}" ]]

cat >"${fake_ossmosis}" <<'EOF'
#!/usr/bin/env bash
exit 1
EOF
if OSSMOSIS_BIN="${fake_ossmosis}" "${INSTALLER}" >"${TEST_ROOT}/stdout" 2>"${TEST_ROOT}/stderr"; then
	echo "Expected an invalid OSSMOSIS_BIN to fail" >&2
	exit 1
fi
grep -q "does not provide ossmosis materialize" "${TEST_ROOT}/stderr"

echo "install_ossmosis tests passed"
