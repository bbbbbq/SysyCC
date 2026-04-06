#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BUILD_DIR="${SYSYCC_COMPILER2025_BUILD_DIR:-${PROJECT_ROOT}/build}"
IR_OUTPUT_DIR="${SYSYCC_COMPILER2025_IR_OUTPUT_DIR:-${PROJECT_ROOT}/build/intermediate_results}"
CASE_BUILD_ROOT_BASE="${SCRIPT_DIR}/build/arm_performance"
COMPILER_BIN="${BUILD_DIR}/SysyCC"
RUNTIME_SOURCE="${SCRIPT_DIR}/sylib.c"
RUNTIME_HEADER="${SCRIPT_DIR}/sylib.h"
RUNTIME_BUILTIN_STUB="${PROJECT_ROOT}/tests/run/support/runtime_builtin_stub.ll"
RUNTIME_COMPAT_SOURCE="${SCRIPT_DIR}/runtime_builtin_compat.c"
RUNTIME_COMPILE_LOG="${CASE_BUILD_ROOT_BASE}/arm_runtime_compile.log"
RUN_STARTED_AT="$(date '+%Y-%m-%d %H:%M:%S %Z')"
CLANG_BASELINE_OPT_LEVEL="${SYSYCC_COMPILER2025_ARM_BASELINE_OPT_LEVEL:--O3}"

source "${PROJECT_ROOT}/tests/test_helpers.sh"
source "${SCRIPT_DIR}/compiler2025_arm_common.sh"

require_positive_integer() {
    local value="$1"
    local option_name="$2"

    if [[ ! "${value}" =~ ^[1-9][0-9]*$ ]]; then
        echo "${option_name} must be a positive integer, got '${value}'" >&2
        exit 1
    fi
}

usage() {
    cat <<'EOF'
Usage:
  ./tests/compiler2025/run_arm_performance.sh [--case-root path] [--report path] [--iterations N] [--warmup N] [case_name ...]

Examples:
  ./tests/compiler2025/run_arm_performance.sh
  ./tests/compiler2025/run_arm_performance.sh --iterations 3 --warmup 1 01_mm1
  ./tests/compiler2025/run_arm_performance.sh --case-root /path/to/ARM-性能 crypto-1
EOF
}

append_result_row() {
    local case_name="$1"
    local status="$2"
    local sysycc_seconds="$3"
    local clang_seconds="$4"
    local perf_percent="$5"
    local detail="$6"
    printf '| %s | %s | %s | %s | %s | %s |\n' \
        "${case_name}" "${status}" "${sysycc_seconds}" "${clang_seconds}" \
        "${perf_percent}" "${detail}" >>"${REPORT_FILE}"
}

write_report_header() {
    mkdir -p "$(dirname "${REPORT_FILE}")"
    cat >"${REPORT_FILE}" <<'EOF'
# Compiler2025 ARM Performance Result

| Case | Status | SysyCC(s) | Clang(s) | Perf | Detail |
| --- | --- | --- | --- | --- | --- |
EOF
}

write_report_metadata() {
    {
        echo
        echo "Generated at: ${RUN_STARTED_AT}"
        echo
        echo "- Case root: ${CASE_ROOT}"
        echo "- Case filter count: ${#SELECTED_CASES[@]}"
        echo "- Warmup runs: ${WARMUP_COUNT}"
        echo "- Timed runs: ${ITERATION_COUNT}"
        echo "- Clang baseline flags: ${CLANG_BASELINE_OPT_LEVEL}"
        echo "- Markdown report: ${REPORT_FILE}"
    } >>"${REPORT_FILE}"
}

write_report_summary() {
    local summary_file=""
    summary_file="$(mktemp)"

    python3 - "${REPORT_FILE}" >"${summary_file}" <<'PY'
from __future__ import annotations

import math
import re
import sys
from pathlib import Path

report = Path(sys.argv[1]).read_text(errors="replace").splitlines()
rows = []
pattern = re.compile(r'^\| (?P<case>.+?) \| (?P<status>.+?) \| (?P<sysycc>.+?) \| (?P<clang>.+?) \| (?P<perf>.+?) \| (?P<detail>.+?) \|$')

for line in report:
    if line.startswith("| Case |") or line.startswith("| --- |"):
        continue
    match = pattern.match(line)
    if match:
        rows.append(match.groupdict())

status_count = {}
measured_perf = []
slowdowns = []
for row in rows:
    status = row["status"]
    status_count[status] = status_count.get(status, 0) + 1
    if status == "PASS":
        sysycc = float(row["sysycc"])
        clang = float(row["clang"])
        measured_perf.append(clang / sysycc)
        slowdowns.append(sysycc / clang)

print("## Summary")
print()
print(f"- Total cases: {len(rows)}")
print(f"- Measured cases: {len(measured_perf)}")
print(f"- Failed cases: {len(rows) - len(measured_perf)}")
for status, count in sorted(status_count.items()):
    print(f"- {status}: {count}")
if measured_perf:
    geo_perf = math.exp(sum(math.log(x) for x in measured_perf) / len(measured_perf))
    geo_slowdown = math.exp(sum(math.log(x) for x in slowdowns) / len(slowdowns))
    print(f"- Geomean relative performance: {geo_perf * 100:.2f}% of Clang")
    print(f"- Geomean slowdown vs Clang: {geo_slowdown:.4f}x")
PY

    {
        echo
        cat "${summary_file}"
    } >>"${REPORT_FILE}"

    rm -f "${summary_file}"
}

format_timing_fields() {
    local sysycc_json="$1"
    local clang_json="$2"

    python3 - "${sysycc_json}" "${clang_json}" <<'PY'
from __future__ import annotations

import json
import sys
from pathlib import Path

sysycc = json.loads(Path(sys.argv[1]).read_text())
clang = json.loads(Path(sys.argv[2]).read_text())
sysycc_median = float(sysycc["median_seconds"])
clang_median = float(clang["median_seconds"])
perf_percent = (clang_median / sysycc_median) * 100.0
print(f"{sysycc_median:.6f}|{clang_median:.6f}|{perf_percent:.2f}%")
PY
}

CASE_ROOT=""
REPORT_FILE=""
SELECTED_CASES=()
ITERATION_COUNT="${SYSYCC_COMPILER2025_ARM_PERF_ITERATIONS:-1}"
WARMUP_COUNT="${SYSYCC_COMPILER2025_ARM_PERF_WARMUP:-0}"

while [[ $# -gt 0 ]]; do
    case "$1" in
    --case-root)
        if [[ $# -lt 2 ]]; then
            echo "missing value for --case-root" >&2
            exit 1
        fi
        CASE_ROOT="$2"
        shift 2
        ;;
    --report)
        if [[ $# -lt 2 ]]; then
            echo "missing value for --report" >&2
            exit 1
        fi
        REPORT_FILE="$2"
        shift 2
        ;;
    --iterations)
        if [[ $# -lt 2 ]]; then
            echo "missing value for --iterations" >&2
            exit 1
        fi
        ITERATION_COUNT="$2"
        shift 2
        ;;
    --warmup)
        if [[ $# -lt 2 ]]; then
            echo "missing value for --warmup" >&2
            exit 1
        fi
        WARMUP_COUNT="$2"
        shift 2
        ;;
    -h|--help)
        usage
        exit 0
        ;;
    --)
        shift
        while [[ $# -gt 0 ]]; do
            SELECTED_CASES+=("$1")
            shift
        done
        ;;
    *)
        SELECTED_CASES+=("$1")
        shift
        ;;
    esac
done

require_positive_integer "${ITERATION_COUNT}" "--iterations"
if [[ ! "${WARMUP_COUNT}" =~ ^[0-9]+$ ]]; then
    echo "--warmup must be a non-negative integer" >&2
    exit 1
fi

CASE_ROOT="$(compiler2025_resolve_case_root "${CASE_ROOT}")"
if [[ -z "${REPORT_FILE}" ]]; then
    REPORT_FILE="${CASE_BUILD_ROOT_BASE}/compiler2025_arm_performance_result.md"
fi

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
mkdir -p "${CASE_BUILD_ROOT_BASE}"
compiler2025_prepare_compiler_snapshot \
    "${COMPILER_BIN}" "${IR_OUTPUT_DIR}" \
    "${CASE_BUILD_ROOT_BASE}/toolchain_snapshot"
COMPILER_BIN="${COMPILER2025_SNAPSHOT_COMPILER_BIN}"
IR_OUTPUT_DIR="${COMPILER2025_SNAPSHOT_IR_OUTPUT_DIR}"
export SYSYCC_INTERMEDIATE_RESULTS_DIR="${IR_OUTPUT_DIR}"
RUNTIME_IR_FILE="$(
    compiler2025_compile_runtime_ir \
        "${COMPILER_BIN}" "${RUNTIME_SOURCE}" "${IR_OUTPUT_DIR}" \
        "${RUNTIME_COMPILE_LOG}"
)"

write_report_header

total_cases=0
passed_cases=0
failed_cases=0

while IFS= read -r -d '' source_file; do
    test_name="$(basename "${source_file}" .sy)"
    input_file="${CASE_ROOT}/${test_name}.in"
    expected_output_file="${CASE_ROOT}/${test_name}.out"
    ir_file="${IR_OUTPUT_DIR}/${test_name}.ll"
    case_dir="${CASE_BUILD_ROOT_BASE}/${test_name}"
    sysycc_binary="${case_dir}/${test_name}.sysycc.bin"
    clang_binary="${case_dir}/${test_name}.clang.bin"
    sysycc_output_file="${case_dir}/${test_name}.sysycc.actual.out"
    clang_output_file="${case_dir}/${test_name}.clang.actual.out"
    sysycc_compiler_log="${case_dir}/${test_name}.sysycc.compile.log"
    sysycc_link_log="${case_dir}/${test_name}.sysycc.link.log"
    clang_compile_log="${case_dir}/${test_name}.clang.compile.log"
    sysycc_stderr_log="${case_dir}/${test_name}.sysycc.stderr.log"
    clang_stderr_log="${case_dir}/${test_name}.clang.stderr.log"
    sysycc_diff_log="${case_dir}/${test_name}.sysycc.diff"
    clang_diff_log="${case_dir}/${test_name}.clang.diff"
    sysycc_timing_json="${case_dir}/${test_name}.sysycc.timing.json"
    clang_timing_json="${case_dir}/${test_name}.clang.timing.json"

    if ! compiler2025_case_matches_filter "${test_name}" "${SELECTED_CASES[@]}"; then
        continue
    fi

    total_cases=$((total_cases + 1))
    mkdir -p "${case_dir}"

    echo "==> [arm-performance/${test_name}] compiling"

    if [[ ! -f "${expected_output_file}" ]]; then
        echo "[FAIL] arm-performance/${test_name}: missing expected output" >&2
        append_result_row "${test_name}" "MISSING_EXPECTED" "-" "-" "-" \
            "$(basename "${expected_output_file}")"
        failed_cases=$((failed_cases + 1))
        continue
    fi

    if ! "${COMPILER_BIN}" -include "${RUNTIME_HEADER}" "${source_file}" \
        --dump-ir >"${sysycc_compiler_log}" 2>&1; then
        echo "[FAIL] arm-performance/${test_name}: SysyCC compile failed" >&2
        append_result_row "${test_name}" "SYSYCC_COMPILE_FAIL" "-" "-" "-" \
            "$(basename "${sysycc_compiler_log}")"
        failed_cases=$((failed_cases + 1))
        continue
    fi

    if [[ ! -f "${ir_file}" || ! -s "${ir_file}" ]]; then
        echo "[FAIL] arm-performance/${test_name}: missing IR output" >&2
        append_result_row "${test_name}" "MISSING_IR" "-" "-" "-" \
            "$(basename "${ir_file}")"
        failed_cases=$((failed_cases + 1))
        continue
    fi

    if ! clang "${ir_file}" "${RUNTIME_IR_FILE}" "${RUNTIME_COMPAT_SOURCE}" \
        "${RUNTIME_BUILTIN_STUB}" -fno-builtin -o "${sysycc_binary}" \
        >"${sysycc_link_log}" 2>&1; then
        echo "[FAIL] arm-performance/${test_name}: SysyCC IR link failed" >&2
        append_result_row "${test_name}" "SYSYCC_LINK_FAIL" "-" "-" "-" \
            "$(basename "${sysycc_link_log}")"
        failed_cases=$((failed_cases + 1))
        continue
    fi

    if ! clang "${CLANG_BASELINE_OPT_LEVEL}" -std=gnu99 -x c \
        -include "${RUNTIME_HEADER}" -I "${SCRIPT_DIR}" \
        "${source_file}" "${RUNTIME_SOURCE}" -o "${clang_binary}" \
        >"${clang_compile_log}" 2>&1; then
        echo "[FAIL] arm-performance/${test_name}: Clang baseline compile failed" >&2
        append_result_row "${test_name}" "CLANG_COMPILE_FAIL" "-" "-" "-" \
            "$(basename "${clang_compile_log}")"
        failed_cases=$((failed_cases + 1))
        continue
    fi

    compiler2025_run_binary_capture "${sysycc_binary}" "${input_file}" \
        "${sysycc_output_file}" "${sysycc_stderr_log}"
    sysycc_exit_code="${COMPILER2025_LAST_EXIT_CODE}"

    if ! compiler2025_compare_expected_output "${expected_output_file}" \
        "${sysycc_output_file}" "${sysycc_diff_log}"; then
        echo "[FAIL] arm-performance/${test_name}: SysyCC output mismatch" >&2
        append_result_row "${test_name}" "SYSYCC_MISMATCH" "-" "-" "-" \
            "$(basename "${sysycc_diff_log}")"
        failed_cases=$((failed_cases + 1))
        continue
    fi

    compiler2025_run_binary_capture "${clang_binary}" "${input_file}" \
        "${clang_output_file}" "${clang_stderr_log}"
    clang_exit_code="${COMPILER2025_LAST_EXIT_CODE}"

    if ! compiler2025_compare_expected_output "${expected_output_file}" \
        "${clang_output_file}" "${clang_diff_log}"; then
        echo "[FAIL] arm-performance/${test_name}: Clang output mismatch" >&2
        append_result_row "${test_name}" "CLANG_MISMATCH" "-" "-" "-" \
            "$(basename "${clang_diff_log}")"
        failed_cases=$((failed_cases + 1))
        continue
    fi

    if ! compiler2025_measure_binary_runtime "${sysycc_binary}" "${input_file}" \
        "${WARMUP_COUNT}" "${ITERATION_COUNT}" "${sysycc_exit_code}" \
        "${sysycc_timing_json}" >"${case_dir}/${test_name}.sysycc.time.log" 2>&1; then
        echo "[FAIL] arm-performance/${test_name}: SysyCC timing failed" >&2
        append_result_row "${test_name}" "SYSYCC_BENCH_FAIL" "-" "-" "-" \
            "$(basename "${case_dir}/${test_name}.sysycc.time.log")"
        failed_cases=$((failed_cases + 1))
        continue
    fi

    if ! compiler2025_measure_binary_runtime "${clang_binary}" "${input_file}" \
        "${WARMUP_COUNT}" "${ITERATION_COUNT}" "${clang_exit_code}" \
        "${clang_timing_json}" >"${case_dir}/${test_name}.clang.time.log" 2>&1; then
        echo "[FAIL] arm-performance/${test_name}: Clang timing failed" >&2
        append_result_row "${test_name}" "CLANG_BENCH_FAIL" "-" "-" "-" \
            "$(basename "${case_dir}/${test_name}.clang.time.log")"
        failed_cases=$((failed_cases + 1))
        continue
    fi

    timing_fields="$(format_timing_fields "${sysycc_timing_json}" "${clang_timing_json}")"
    sysycc_seconds="${timing_fields%%|*}"
    remainder="${timing_fields#*|}"
    clang_seconds="${remainder%%|*}"
    perf_percent="${timing_fields##*|}"

    passed_cases=$((passed_cases + 1))
    append_result_row "${test_name}" "PASS" "${sysycc_seconds}" \
        "${clang_seconds}" "${perf_percent}" "ok"
    echo "[PASS] arm-performance/${test_name}: ${perf_percent} of Clang"
done < <(find "${CASE_ROOT}" -maxdepth 1 -type f -name '*.sy' -print0 | sort -z)

write_report_metadata
write_report_summary

echo
echo "==> ARM Performance Summary"
echo "- Total: ${total_cases}"
echo "- Measured: ${passed_cases}"
echo "- Failed: ${failed_cases}"
echo "- Markdown report: ${REPORT_FILE}"

if [[ "${total_cases}" -eq 0 ]]; then
    echo "no matching cases were selected" >&2
    exit 1
fi

if [[ "${failed_cases}" -ne 0 ]]; then
    exit 1
fi
