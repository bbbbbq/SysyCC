#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
TEST_TMP_DIR="$(mktemp -d)"
FAKE_BIN_DIR="${TEST_TMP_DIR}/bin"
DOCKER_LOG="${TEST_TMP_DIR}/docker.log"
CASE_ROOT="${TEST_TMP_DIR}/arm-cases"
REPORT_ROOT="${TEST_TMP_DIR}/reports"
REPORT_FILE="${REPORT_ROOT}/arm-performance.md"
RESOLVED_CASE_ROOT="$(python3 - "${CASE_ROOT}" <<'PY'
from pathlib import Path
import sys

print(Path(sys.argv[1]).resolve(strict=False))
PY
)"
RESOLVED_REPORT_ROOT="$(python3 - "${REPORT_ROOT}" <<'PY'
from pathlib import Path
import sys

print(Path(sys.argv[1]).resolve(strict=False))
PY
)"

cleanup() {
    rm -rf "${TEST_TMP_DIR}"
}
trap cleanup EXIT

mkdir -p "${FAKE_BIN_DIR}" "${CASE_ROOT}" "${REPORT_ROOT}"

cat >"${FAKE_BIN_DIR}/docker" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$@" >"${DOCKER_LOG}"
EOF
chmod +x "${FAKE_BIN_DIR}/docker"

PATH="${FAKE_BIN_DIR}:${PATH}" \
DOCKER_LOG="${DOCKER_LOG}" \
    "${PROJECT_ROOT}/tests/compiler2025/run_arm_performance_in_docker.sh" \
    --no-build \
    --case-root "${CASE_ROOT}" \
    --report "${REPORT_FILE}" \
    --iterations 1 \
    sample_case

grep -Fx 'run' "${DOCKER_LOG}"
grep -Fx "${PROJECT_ROOT}:/workspace" "${DOCKER_LOG}"
grep -Fx "${RESOLVED_CASE_ROOT}:/compiler2025-case-root:ro" "${DOCKER_LOG}"
grep -Fx "${RESOLVED_REPORT_ROOT}:/compiler2025-report-root:rw" "${DOCKER_LOG}"
grep -Fqx './tests/compiler2025/run_arm_performance.sh --case-root /compiler2025-case-root --report /compiler2025-report-root/arm-performance.md --iterations 1 sample_case' "${DOCKER_LOG}"

echo "verified: ARM performance docker wrapper remaps external case/report paths into the container"
