#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BUILD_DIR="${SYSYCC_COMPILER2025_BUILD_DIR:-${PROJECT_ROOT}/build}"
IR_OUTPUT_DIR="${SYSYCC_COMPILER2025_IR_OUTPUT_DIR:-${PROJECT_ROOT}/build/intermediate_results}"
CASE_BUILD_ROOT_BASE="${SCRIPT_DIR}/build/arm_functional"
COMPILER_BIN="${BUILD_DIR}/SysyCC"
RUNTIME_SOURCE="${SCRIPT_DIR}/sylib.c"
RUNTIME_HEADER="${SCRIPT_DIR}/sylib.h"
RUNTIME_BUILTIN_STUB="${PROJECT_ROOT}/tests/run/support/runtime_builtin_stub.ll"
RUNTIME_COMPAT_SOURCE="${SCRIPT_DIR}/runtime_builtin_compat.c"
RUNTIME_COMPILE_LOG="${CASE_BUILD_ROOT_BASE}/arm_runtime_compile.log"
RUN_STARTED_AT="$(date '+%Y-%m-%d %H:%M:%S %Z')"

source "${PROJECT_ROOT}/tests/test_helpers.sh"
source "${SCRIPT_DIR}/compiler2025_arm_common.sh"

usage() {
    cat <<'EOF'
Usage:
  ./tests/compiler2025/run_arm_functional.sh [--case-root path] [--report path] [case_name ...]

Examples:
  ./tests/compiler2025/run_arm_functional.sh
  ./tests/compiler2025/run_arm_functional.sh --case-root /path/to/ARM-性能
  ./tests/compiler2025/run_arm_functional.sh 01_mm1 crypto-1
EOF
}

append_result_row() {
    local case_name="$1"
    local status="$2"
    local detail="$3"
    printf '| %s | %s | %s |\n' \
        "${case_name}" "${status}" "${detail}" >>"${REPORT_FILE}"
}

write_report_header() {
    mkdir -p "$(dirname "${REPORT_FILE}")"
    cat >"${REPORT_FILE}" <<'EOF'
# Compiler2025 ARM Functional Result

| Case | Status | Detail |
| --- | --- | --- |
EOF
}

write_report_metadata() {
    {
        echo
        echo "Generated at: ${RUN_STARTED_AT}"
        echo
        echo "- Case root: ${CASE_ROOT}"
        echo "- Case filter count: ${#SELECTED_CASES[@]}"
        echo "- Markdown report: ${REPORT_FILE}"
    } >>"${REPORT_FILE}"
}

write_report_summary() {
    local summary_file=""
    summary_file="$(mktemp)"

    awk '
        /^\| [^ ]+ \| [^ ]+ \| [^ ]+ \|/ {
            if ($0 ~ /^\| Case \|/ || $0 ~ /^\| --- \|/) {
                next;
            }

            line = $0;
            gsub(/^\| /, "", line);
            gsub(/ \|$/, "", line);
            split(line, parts, " \\| ");
            status = parts[2];
            total += 1;
            if (status == "PASS") {
                passed += 1;
            } else {
                failed += 1;
            }
            status_count[status] += 1;
        }
        END {
            print "## Summary";
            print "";
            printf("- Total cases: %d\n", total);
            printf("- Passed: %d\n", passed);
            printf("- Failed: %d\n", failed);
            for (status in status_count) {
                printf("- %s: %d\n", status, status_count[status]);
            }
        }
    ' "${REPORT_FILE}" >"${summary_file}"

    {
        echo
        cat "${summary_file}"
    } >>"${REPORT_FILE}"

    rm -f "${summary_file}"
}

CASE_ROOT=""
REPORT_FILE=""
SELECTED_CASES=()

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

CASE_ROOT="$(compiler2025_resolve_case_root "${CASE_ROOT}")"
if [[ -z "${REPORT_FILE}" ]]; then
    REPORT_FILE="${CASE_BUILD_ROOT_BASE}/compiler2025_arm_functional_result.md"
fi

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
mkdir -p "${CASE_BUILD_ROOT_BASE}"
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
    program_binary="${case_dir}/${test_name}.bin"
    actual_output_file="${case_dir}/${test_name}.actual.out"
    compiler_log_file="${case_dir}/${test_name}.compile.log"
    link_log_file="${case_dir}/${test_name}.link.log"
    stderr_log_file="${case_dir}/${test_name}.stderr.log"
    diff_log_file="${case_dir}/${test_name}.diff"

    if ! compiler2025_case_matches_filter "${test_name}" "${SELECTED_CASES[@]}"; then
        continue
    fi

    total_cases=$((total_cases + 1))
    mkdir -p "${case_dir}"

    echo "==> [arm-functional/${test_name}] compiling"
    if ! "${COMPILER_BIN}" -include "${RUNTIME_HEADER}" "${source_file}" \
        --dump-ir >"${compiler_log_file}" 2>&1; then
        echo "[FAIL] arm-functional/${test_name}: compiler failed" >&2
        append_result_row "${test_name}" "SYSYCC_COMPILE_FAIL" \
            "$(basename "${compiler_log_file}")"
        failed_cases=$((failed_cases + 1))
        continue
    fi

    if [[ ! -f "${expected_output_file}" ]]; then
        echo "[FAIL] arm-functional/${test_name}: missing expected output" >&2
        append_result_row "${test_name}" "MISSING_EXPECTED" \
            "$(basename "${expected_output_file}")"
        failed_cases=$((failed_cases + 1))
        continue
    fi

    if [[ ! -f "${ir_file}" || ! -s "${ir_file}" ]]; then
        echo "[FAIL] arm-functional/${test_name}: missing IR output" >&2
        append_result_row "${test_name}" "MISSING_IR" \
            "$(basename "${ir_file}")"
        failed_cases=$((failed_cases + 1))
        continue
    fi

    if ! clang "${ir_file}" "${RUNTIME_IR_FILE}" "${RUNTIME_COMPAT_SOURCE}" \
        "${RUNTIME_BUILTIN_STUB}" -fno-builtin -o "${program_binary}" \
        >"${link_log_file}" 2>&1; then
        echo "[FAIL] arm-functional/${test_name}: IR link failed" >&2
        append_result_row "${test_name}" "SYSYCC_LINK_FAIL" \
            "$(basename "${link_log_file}")"
        failed_cases=$((failed_cases + 1))
        continue
    fi

    compiler2025_run_binary_capture "${program_binary}" "${input_file}" \
        "${actual_output_file}" "${stderr_log_file}"

    if ! compiler2025_compare_expected_output "${expected_output_file}" \
        "${actual_output_file}" "${diff_log_file}"; then
        echo "[FAIL] arm-functional/${test_name}: output mismatch" >&2
        append_result_row "${test_name}" "MISMATCH" \
            "$(basename "${diff_log_file}")"
        failed_cases=$((failed_cases + 1))
        continue
    fi

    rm -f "${diff_log_file}"
    passed_cases=$((passed_cases + 1))
    append_result_row "${test_name}" "PASS" "ok"
    echo "[PASS] arm-functional/${test_name}"
done < <(find "${CASE_ROOT}" -maxdepth 1 -type f -name '*.sy' -print0 | sort -z)

write_report_metadata
write_report_summary

echo
echo "==> ARM Functional Summary"
echo "- Total: ${total_cases}"
echo "- Passed: ${passed_cases}"
echo "- Failed: ${failed_cases}"
echo "- Markdown report: ${REPORT_FILE}"

if [[ "${total_cases}" -eq 0 ]]; then
    echo "no matching cases were selected" >&2
    exit 1
fi

if [[ "${failed_cases}" -ne 0 ]]; then
    exit 1
fi
