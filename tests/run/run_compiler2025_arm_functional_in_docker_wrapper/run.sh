#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
TEST_TMP_DIR="$(mktemp -d)"
FAKE_BIN_DIR="${TEST_TMP_DIR}/bin"
DOCKER_LOG="${TEST_TMP_DIR}/docker.log"
CASE_ROOT="${TEST_TMP_DIR}/arm-cases"
RESOLVED_CASE_ROOT="$(python3 - "${CASE_ROOT}" <<'PY'
from pathlib import Path
import sys

print(Path(sys.argv[1]).resolve(strict=False))
PY
)"

cleanup() {
    rm -rf "${TEST_TMP_DIR}"
}
trap cleanup EXIT

mkdir -p "${FAKE_BIN_DIR}" "${CASE_ROOT}"

cat >"${FAKE_BIN_DIR}/docker" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$@" >"${DOCKER_LOG}"
EOF
chmod +x "${FAKE_BIN_DIR}/docker"

PATH="${FAKE_BIN_DIR}:${PATH}" \
DOCKER_LOG="${DOCKER_LOG}" \
    "${PROJECT_ROOT}/tests/compiler2025/run_arm_functional_in_docker.sh" \
    --no-build \
    --case-root "${CASE_ROOT}" \
    sample_case

grep -Fx 'run' "${DOCKER_LOG}"
grep -Fx "${PROJECT_ROOT}:/workspace" "${DOCKER_LOG}"
grep -Fx "${RESOLVED_CASE_ROOT}:/compiler2025-case-root:ro" "${DOCKER_LOG}"
grep -Fqx './tests/compiler2025/run_arm_functional.sh --case-root /compiler2025-case-root sample_case' "${DOCKER_LOG}"

echo "verified: ARM functional docker wrapper mounts external case roots and forwards script args"
