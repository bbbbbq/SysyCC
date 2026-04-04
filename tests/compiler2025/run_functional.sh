#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BUILD_DIR="${SYSYCC_COMPILER2025_BUILD_DIR:-${PROJECT_ROOT}/build}"
IR_OUTPUT_DIR="${SYSYCC_COMPILER2025_IR_OUTPUT_DIR:-${PROJECT_ROOT}/build/intermediate_results}"
CASE_BUILD_ROOT_BASE="${SCRIPT_DIR}/build"
COMPILER_BIN="${BUILD_DIR}/SysyCC"
RUNTIME_SOURCE="${SCRIPT_DIR}/sylib.c"
RUNTIME_BUILTIN_STUB="${PROJECT_ROOT}/tests/run/support/runtime_builtin_stub.ll"
RUNTIME_COMPAT_SOURCE="${SCRIPT_DIR}/runtime_builtin_compat.c"
RUNTIME_IR_FILE="${IR_OUTPUT_DIR}/sylib.ll"
RUNTIME_COMPILE_LOG="${CASE_BUILD_ROOT_BASE}/compiler2025_runtime_compile.log"
RUN_STARTED_AT="$(date '+%Y-%m-%d %H:%M:%S %Z')"

usage() {
    cat <<'EOF'
Usage:
  ./tests/compiler2025/run_functional.sh [--suite functional|h_functional|all] [--report path] [case_name ...]

Examples:
  ./tests/compiler2025/run_functional.sh
  ./tests/compiler2025/run_functional.sh --suite h_functional
  ./tests/compiler2025/run_functional.sh --suite all 00_main 00_comment2
EOF
}

get_case_root() {
    local suite_name="$1"
    case "${suite_name}" in
    functional)
        printf '%s\n' \
            "${SCRIPT_DIR}/extracted/functional/functional_recover/functional"
        ;;
    h_functional)
        printf '%s\n' \
            "${SCRIPT_DIR}/extracted/functional/functional_recover/h_functional"
        ;;
    *)
        return 1
        ;;
    esac
}

append_result_row() {
    local suite_name="$1"
    local case_name="$2"
    local status="$3"
    local detail="$4"
    printf '| %s | %s | %s | %s |\n' \
        "${suite_name}" "${case_name}" "${status}" "${detail}" >>"${REPORT_FILE}"
}

write_report_header() {
    mkdir -p "$(dirname "${REPORT_FILE}")"
    cat >"${REPORT_FILE}" <<'EOF'
# Compiler2025 Functional Result

| Suite | Case | Status | Detail |
| --- | --- | --- | --- |
EOF
}

write_report_metadata() {
    {
        echo
        echo "Generated at: ${RUN_STARTED_AT}"
        echo
        echo "- Suite selection: ${SUITE}"
        echo "- Case filter count: ${#SELECTED_CASES[@]}"
        echo "- Markdown report: ${REPORT_FILE}"
        echo "- functional root: $(get_case_root functional)"
        echo "- h_functional root: $(get_case_root h_functional)"
    } >>"${REPORT_FILE}"
}

write_report_summary() {
    local summary_file=""
    summary_file="$(mktemp)"

    awk '
        /^\| [^ ]+ \| [^ ]+ \| [^ ]+ \|/ {
            if ($0 ~ /^\| Suite \|/ || $0 ~ /^\| --- \|/) {
                next;
            }

            line = $0;
            gsub(/^\| /, "", line);
            gsub(/ \|$/, "", line);
            split(line, parts, " \\| ");
            suite = parts[1];
            status = parts[3];
            total += 1;
            suite_total[suite] += 1;
            if (status == "PASS") {
                passed += 1;
                suite_passed[suite] += 1;
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
            if (total > 0) {
                print "";
                print "## Suite Summary";
                print "";
                for (suite in suite_total) {
                    printf("- %s: %d/%d passed\n",
                           suite, suite_passed[suite], suite_total[suite]);
                }
            }
        }
    ' "${REPORT_FILE}" >"${summary_file}"

    {
        echo
        cat "${summary_file}"
    } >>"${REPORT_FILE}"

    rm -f "${summary_file}"
}

source "${PROJECT_ROOT}/tests/test_helpers.sh"

if [[ ! -f "${RUNTIME_SOURCE}" ]]; then
    echo "missing runtime source: ${RUNTIME_SOURCE}" >&2
    exit 1
fi

if [[ ! -f "${RUNTIME_COMPAT_SOURCE}" ]]; then
    echo "missing runtime compat source: ${RUNTIME_COMPAT_SOURCE}" >&2
    exit 1
fi

SUITE="functional"
REPORT_FILE=""
SELECTED_CASES=()

while [[ $# -gt 0 ]]; do
    case "$1" in
    --suite)
        if [[ $# -lt 2 ]]; then
            echo "missing value for --suite" >&2
            exit 1
        fi
        SUITE="$2"
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

case "${SUITE}" in
functional|h_functional|all)
    ;;
*)
    echo "unsupported suite: ${SUITE}" >&2
    exit 1
    ;;
esac

if [[ -z "${REPORT_FILE}" ]]; then
    REPORT_FILE="${CASE_BUILD_ROOT_BASE}/compiler2025_${SUITE}_result.md"
fi

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

if ! "${COMPILER_BIN}" "${RUNTIME_SOURCE}" --dump-ir >"${RUNTIME_COMPILE_LOG}" 2>&1; then
    echo "failed to compile compiler2025 runtime with SysyCC, see ${RUNTIME_COMPILE_LOG}" >&2
    exit 1
fi

if [[ ! -f "${RUNTIME_IR_FILE}" || ! -s "${RUNTIME_IR_FILE}" ]]; then
    echo "missing runtime IR output: ${RUNTIME_IR_FILE}" >&2
    exit 1
fi

mkdir -p "${CASE_BUILD_ROOT_BASE}"
write_report_header

total_cases=0
passed_cases=0
failed_cases=0
SUITES_TO_RUN=()
if [[ "${SUITE}" == "all" ]]; then
    SUITES_TO_RUN=(functional h_functional)
else
    SUITES_TO_RUN=("${SUITE}")
fi

case_matches_filter() {
    local case_name="$1"
    if [[ "${#SELECTED_CASES[@]}" -eq 0 ]]; then
        return 0
    fi
    for selected_case in "${SELECTED_CASES[@]}"; do
        if [[ "${case_name}" == "${selected_case}" ]]; then
            return 0
        fi
    done
    return 1
}

for suite_name in "${SUITES_TO_RUN[@]}"; do
    case_root="$(get_case_root "${suite_name}")"
    case_build_root="${CASE_BUILD_ROOT_BASE}/${suite_name}"

    if [[ ! -d "${case_root}" ]]; then
        echo "missing case directory: ${case_root}" >&2
        exit 1
    fi

    mkdir -p "${case_build_root}"

    while IFS= read -r -d '' source_file; do
        test_name="$(basename "${source_file}" .sy)"
        input_file="${case_root}/${test_name}.in"
        expected_output_file="${case_root}/${test_name}.out"
        ir_file="${IR_OUTPUT_DIR}/${test_name}.ll"
        case_dir="${case_build_root}/${test_name}"
        program_binary="${case_dir}/${test_name}.bin"
        actual_output_file="${case_dir}/${test_name}.actual.out"
        compiler_log_file="${case_dir}/${test_name}.compile.log"
        link_log_file="${case_dir}/${test_name}.link.log"
        stderr_log_file="${case_dir}/${test_name}.stderr.log"
        diff_log_file="${case_dir}/${test_name}.diff"

        if ! case_matches_filter "${test_name}"; then
            continue
        fi

        total_cases=$((total_cases + 1))
        mkdir -p "${case_dir}"

        echo "==> [${suite_name}/${test_name}] compiling"
        if ! "${COMPILER_BIN}" "${source_file}" --dump-ir >"${compiler_log_file}" 2>&1; then
            echo "[FAIL] ${suite_name}/${test_name}: compiler failed" >&2
            append_result_row "${suite_name}" "${test_name}" \
                "SYSYCC_COMPILE_FAIL" "$(basename "${compiler_log_file}")"
            failed_cases=$((failed_cases + 1))
            continue
        fi

        if [[ ! -f "${expected_output_file}" ]]; then
            echo "[FAIL] ${suite_name}/${test_name}: missing expected output" >&2
            append_result_row "${suite_name}" "${test_name}" \
                "MISSING_EXPECTED" "$(basename "${expected_output_file}")"
            failed_cases=$((failed_cases + 1))
            continue
        fi

        if [[ ! -f "${ir_file}" || ! -s "${ir_file}" ]]; then
            echo "[FAIL] ${suite_name}/${test_name}: missing IR output" >&2
            append_result_row "${suite_name}" "${test_name}" \
                "MISSING_IR" "$(basename "${ir_file}")"
            failed_cases=$((failed_cases + 1))
            continue
        fi

        if ! clang "${ir_file}" "${RUNTIME_IR_FILE}" "${RUNTIME_COMPAT_SOURCE}" \
            "${RUNTIME_BUILTIN_STUB}" -fno-builtin -o "${program_binary}" \
            >"${link_log_file}" 2>&1; then
            echo "[FAIL] ${suite_name}/${test_name}: IR link failed" >&2
            append_result_row "${suite_name}" "${test_name}" \
                "SYSYCC_LINK_FAIL" "$(basename "${link_log_file}")"
            failed_cases=$((failed_cases + 1))
            continue
        fi

        set +e
        if [[ -f "${input_file}" ]]; then
            "${program_binary}" <"${input_file}" >"${actual_output_file}" \
                2>"${stderr_log_file}"
        else
            "${program_binary}" >"${actual_output_file}" 2>"${stderr_log_file}"
        fi
        program_exit_code=$?
        set -e

        python3 - "${actual_output_file}" "${program_exit_code}" <<'PY'
from pathlib import Path
import sys

output_path = Path(sys.argv[1])
exit_code = sys.argv[2]
content = output_path.read_text()
if content and not content.endswith("\n"):
    content += "\n"
content += f"{exit_code}\n"
output_path.write_text(content)
PY

        if ! python3 - "${expected_output_file}" "${actual_output_file}" \
            >"${diff_log_file}" 2>&1 <<'PY'
from pathlib import Path
import difflib
import sys

expected_path = Path(sys.argv[1])
actual_path = Path(sys.argv[2])
expected = expected_path.read_text(errors="replace").replace("\r\n", "\n").replace("\r", "\n")
actual = actual_path.read_text(errors="replace").replace("\r\n", "\n").replace("\r", "\n")
if expected != actual:
    diff = difflib.unified_diff(
        expected.splitlines(True),
        actual.splitlines(True),
        fromfile=str(expected_path),
        tofile=str(actual_path),
    )
    sys.stdout.writelines(diff)
    sys.exit(1)
PY
        then
            echo "[FAIL] ${suite_name}/${test_name}: output mismatch" >&2
            append_result_row "${suite_name}" "${test_name}" \
                "MISMATCH" "$(basename "${diff_log_file}")"
            failed_cases=$((failed_cases + 1))
            continue
        fi

        rm -f "${diff_log_file}"
        passed_cases=$((passed_cases + 1))
        append_result_row "${suite_name}" "${test_name}" "PASS" "ok"
        echo "[PASS] ${suite_name}/${test_name}"
    done < <(find "${case_root}" -maxdepth 1 -type f -name '*.sy' -print0 | sort -z)
done

write_report_metadata
write_report_summary

echo
echo "==> Functional Summary"
echo "- Suite selection: ${SUITE}"
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
