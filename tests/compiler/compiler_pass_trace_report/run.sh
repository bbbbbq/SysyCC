#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
CASE_BUILD_DIR="${SCRIPT_DIR}/build"
REPORT_DIR="${CASE_BUILD_DIR}/reports"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
rm -rf "${CASE_BUILD_DIR}"
mkdir -p "${CASE_BUILD_DIR}" "${REPORT_DIR}"

cat >"${CASE_BUILD_DIR}/sample.c" <<'EOF'
int helper(int n) {
    int s;
    int i;
    s = 0;
    i = 0;
    while (i < n) {
        s = s + i;
        i = i + 1;
    }
    return s;
}

int main(void) {
    return helper(4);
}
EOF

SYSYCC_PASS_REPORT_DIR="${REPORT_DIR}" \
    "${BUILD_DIR}/compiler" \
        -O1 \
        -S \
        -emit-llvm \
        "${CASE_BUILD_DIR}/sample.c" \
        -o "${CASE_BUILD_DIR}/sample.ll"

reports=()
while IFS= read -r report_file; do
    reports+=("${report_file}")
done < <(find "${REPORT_DIR}" -maxdepth 1 -type f -name '*.md' | sort)
if [[ "${#reports[@]}" -ne 1 ]]; then
    echo "error: expected exactly one pass report, found ${#reports[@]}" >&2
    exit 1
fi

report="${reports[0]}"
grep -q "# SysyCC Pass Trace Report" "${report}"
grep -q "## Top 10 Slow Passes" "${report}"
grep -q "## Fixed-Point Groups" "${report}"
grep -q "## Pass Timeline" "${report}"
grep -q "Instructions delta" "${report}"
grep -q "CoreIrInstCombinePass" "${report}"
grep -q "group 1 iter 1" "${report}"
grep -Eq '[0-9]+ -> [0-9]+ \([+-][0-9]+\)' "${report}"

diff_report="${CASE_BUILD_DIR}/pass-report-diff.md"
python3 "${PROJECT_ROOT}/tests/manual/external_real_project_probe/diff_pass_reports.py" \
    "${report}" \
    "${report}" \
    -o "${diff_report}"
grep -q "# SysyCC Pass Report Diff" "${diff_report}"
grep -q "## Summary" "${diff_report}"
grep -q "## Top 15 Pass Time Changes" "${diff_report}"
grep -q "## Fixed-Point Groups" "${diff_report}"
grep -q "Pipeline wall ms" "${diff_report}"
grep -q "Final instructions" "${diff_report}"
grep -q "CoreIr" "${diff_report}"
grep -q "+0.00%" "${diff_report}"

echo "verified: pass trace report records slow passes, IR size deltas, fixed-point iterations, and diff summaries"
