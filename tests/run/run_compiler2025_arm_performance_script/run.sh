#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
TEST_BUILD_DIR="${SCRIPT_DIR}/build"
CASE_ROOT="${TEST_BUILD_DIR}/cases"
REPORT_FILE="${TEST_BUILD_DIR}/compiler2025_arm_performance_smoke.md"

mkdir -p "${CASE_ROOT}"

cat >"${CASE_ROOT}/tiny.sy" <<'EOF'
int getint(void);
void putint(int value);
void putch(int value);

int main(void) {
    int value = getint();
    int total = value;
    int i = 0;
    while (i < 128) {
        total = total + i;
        i = i + 1;
    }
    putint(total);
    putch(10);
    return 3;
}
EOF

cat >"${CASE_ROOT}/tiny.in" <<'EOF'
5
EOF

cat >"${CASE_ROOT}/tiny.out" <<'EOF'
8133
3
EOF

"${PROJECT_ROOT}/tests/compiler2025/run_arm_performance.sh" \
    --case-root "${CASE_ROOT}" \
    --report "${REPORT_FILE}" \
    --iterations 1 \
    --warmup 0 \
    tiny

grep -q '| tiny | PASS |' "${REPORT_FILE}"
grep -q 'Compiler2025 ARM Performance Result' "${REPORT_FILE}"
grep -q 'Geomean relative performance:' "${REPORT_FILE}"

echo "verified: compiler2025 ARM performance runner produces one-case timing output"
