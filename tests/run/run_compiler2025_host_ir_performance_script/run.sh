#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
TEST_BUILD_DIR="${SCRIPT_DIR}/build"
CASE_ROOT="${TEST_BUILD_DIR}/cases"
REPORT_FILE="${TEST_BUILD_DIR}/compiler2025_host_ir_performance_timeout.md"
JSON_FILE="${TEST_BUILD_DIR}/compiler2025_host_ir_performance_timeout.json"
IR_OUTPUT_DIR="${TEST_BUILD_DIR}/intermediate_results"

mkdir -p "${CASE_ROOT}"
mkdir -p "${IR_OUTPUT_DIR}"

cat >"${CASE_ROOT}/hang.sy" <<'EOF'
int main(void) {
    while (1) {
    }
    return 0;
}
EOF

: >"${CASE_ROOT}/hang.out"

set +e
SYSYCC_COMPILER2025_BUILD_DIR="${PROJECT_ROOT}/build-ninja" \
SYSYCC_COMPILER2025_IR_OUTPUT_DIR="${IR_OUTPUT_DIR}" \
perl -e 'alarm shift @ARGV; exec @ARGV' 30 \
    "${PROJECT_ROOT}/tests/compiler2025/run_host_ir_performance.sh" \
    --case-root "${CASE_ROOT}" \
    --report "${REPORT_FILE}" \
    --json-report "${JSON_FILE}" \
    --iterations 1 \
    --warmup 0 \
    --compile-timeout 10 \
    --run-timeout 1 \
    hang
status=$?
set -e

if [[ "${status}" -eq 0 ]]; then
    echo "expected timeout case to fail" >&2
    exit 1
fi

grep -Eq '\| hang \| SYSYCC(_COMPILE)?_TIMEOUT \|' "${REPORT_FILE}"
grep -Eq '"status": "SYSYCC(_COMPILE)?_TIMEOUT"' "${JSON_FILE}"

echo "verified: compiler2025 host IR performance runner reports timeouts without hanging"
