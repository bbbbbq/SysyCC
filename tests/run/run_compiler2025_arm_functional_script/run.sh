#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
CASE_ROOT="${SCRIPT_DIR}/cases"
REPORT_FILE="${SCRIPT_DIR}/build/compiler2025_arm_functional_smoke.md"

mkdir -p "${CASE_ROOT}" "${SCRIPT_DIR}/build"

cat >"${CASE_ROOT}/tiny.sy" <<'EOF'
int getint(void);
void putint(int value);
void putch(int value);

int main(void) {
    int value = getint();
    putint(value + 1);
    putch(10);
    return 7;
}
EOF

cat >"${CASE_ROOT}/tiny.in" <<'EOF'
41
EOF

cat >"${CASE_ROOT}/tiny.out" <<'EOF'
42
7
EOF

"${PROJECT_ROOT}/tests/compiler2025/run_arm_functional.sh" \
    --case-root "${CASE_ROOT}" \
    --report "${REPORT_FILE}" \
    tiny

grep -q '| tiny | PASS | ok |' "${REPORT_FILE}"
grep -q 'Compiler2025 ARM Functional Result' "${REPORT_FILE}"

echo "verified: compiler2025 ARM functional runner executes a filtered smoke case"

