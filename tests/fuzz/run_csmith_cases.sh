#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
SYSYCC_BIN="${BUILD_DIR}/SysyCC"
CSMITH_RUNTIME_DIR="${PROJECT_ROOT}/tools/csmith/runtime"
CSMITH_BUILD_RUNTIME_DIR="${PROJECT_ROOT}/tools/csmith/build/runtime"
RUN_TIMEOUT_SECONDS="${RUN_TIMEOUT_SECONDS:-10}"
RESULT_FILE="${SCRIPT_DIR}/result.md"
PRINT_INTERMEDIATE_MODE="${PRINT_INTERMEDIATE_MODE:-never}"

print_usage() {
    echo "usage: $0 all | <case-id> [<case-id> ...]" >&2
    echo "examples:" >&2
    echo "  $0 all" >&2
    echo "  $0 001" >&2
    echo "  $0 001 004 007" >&2
    echo >&2
    echo "environment:" >&2
    echo "  PRINT_INTERMEDIATE_MODE=never|failure|always  (default: never)" >&2
}

normalize_case_id() {
    local raw_id="$1"
    if ! [[ "${raw_id}" =~ ^[0-9]+$ ]]; then
        echo "invalid case id: ${raw_id}" >&2
        exit 1
    fi
    printf "%03d" "${raw_id}"
}

append_result_row() {
    local case_id="$1"
    local status="$2"
    local detail="$3"
    printf '| %s | %s | %s |\n' "${case_id}" "${status}" "${detail}" >>"${RESULT_FILE}"
}

initialize_result_file() {
    cat >"${RESULT_FILE}" <<'EOF'
# Fuzz Result

| Case | Status | Detail |
| --- | --- | --- |
EOF
}

copy_optional_intermediate_file() {
    local case_name="$1"
    local suffix="$2"
    local destination_file="$3"
    local source_file="${BUILD_DIR}/intermediate_results/${case_name}.${suffix}"

    if [[ -f "${source_file}" ]]; then
        cp "${source_file}" "${destination_file}"
    fi
}

copy_sysycc_intermediates() {
    local case_name="$1"
    local case_dir="$2"

    copy_optional_intermediate_file "${case_name}" "preprocessed.sy" \
        "${case_dir}/${case_name}.preprocessed.sy"
    copy_optional_intermediate_file "${case_name}" "tokens.txt" \
        "${case_dir}/${case_name}.tokens.txt"
    copy_optional_intermediate_file "${case_name}" "parse.txt" \
        "${case_dir}/${case_name}.parse.txt"
    copy_optional_intermediate_file "${case_name}" "ast.txt" \
        "${case_dir}/${case_name}.ast.txt"
    copy_optional_intermediate_file "${case_name}" "ll" \
        "${case_dir}/${case_name}.ll"
}

should_print_intermediate_report() {
    local status="$1"

    case "${PRINT_INTERMEDIATE_MODE}" in
    never)
        return 1
        ;;
    always)
        return 0
        ;;
    failure)
        [[ "${status}" != "MATCH" ]]
        return
        ;;
    *)
        echo "invalid PRINT_INTERMEDIATE_MODE: ${PRINT_INTERMEDIATE_MODE}" >&2
        exit 1
        ;;
    esac
}

print_artifact_path_if_exists() {
    local title="$1"
    local file_path="$2"

    if [[ ! -f "${file_path}" ]]; then
        return
    fi

    echo "- ${title}: ${file_path}"
}

print_case_intermediate_report() {
    local case_id="$1"
    local status="$2"
    local detail="$3"
    local case_dir="${SCRIPT_DIR}/${case_id}"
    local case_name="fuzz_${case_id}"
    local case_file="${case_dir}/${case_name}.c"
    local stdin_file="${case_dir}/${case_name}.input.txt"

    if ! should_print_intermediate_report "${status}"; then
        return
    fi

    echo
    echo "## Case ${case_id} ${status}"
    echo "detail: ${detail}"
    echo "source: ${case_file}"
    echo "stdin: ${stdin_file}"
    echo "artifacts:"

    print_artifact_path_if_exists "Clang Compile Exit" \
        "${case_dir}/${case_name}.clang.compile.exit.txt"
    print_artifact_path_if_exists "Clang Compile Stdout" \
        "${case_dir}/${case_name}.clang.compile.stdout.txt"
    print_artifact_path_if_exists "Clang Compile Stderr" \
        "${case_dir}/${case_name}.clang.compile.stderr.txt"
    print_artifact_path_if_exists "Clang Run Exit" \
        "${case_dir}/${case_name}.clang.exit.txt"
    print_artifact_path_if_exists "Clang Run Stdout" \
        "${case_dir}/${case_name}.clang.stdout.txt"
    print_artifact_path_if_exists "Clang Run Stderr" \
        "${case_dir}/${case_name}.clang.stderr.txt"

    print_artifact_path_if_exists "SysyCC Compile Exit" \
        "${case_dir}/${case_name}.sysycc.compile.exit.txt"
    print_artifact_path_if_exists "SysyCC Compile Stdout" \
        "${case_dir}/${case_name}.sysycc.compile.stdout.txt"
    print_artifact_path_if_exists "SysyCC Compile Stderr" \
        "${case_dir}/${case_name}.sysycc.compile.stderr.txt"
    print_artifact_path_if_exists "SysyCC Preprocessed" \
        "${case_dir}/${case_name}.preprocessed.sy"
    print_artifact_path_if_exists "SysyCC Tokens" \
        "${case_dir}/${case_name}.tokens.txt"
    print_artifact_path_if_exists "SysyCC Parse" \
        "${case_dir}/${case_name}.parse.txt"
    print_artifact_path_if_exists "SysyCC AST" \
        "${case_dir}/${case_name}.ast.txt"
    print_artifact_path_if_exists "SysyCC LLVM IR" \
        "${case_dir}/${case_name}.ll"

    print_artifact_path_if_exists "SysyCC Link Exit" \
        "${case_dir}/${case_name}.sysycc.link.exit.txt"
    print_artifact_path_if_exists "SysyCC Link Stdout" \
        "${case_dir}/${case_name}.sysycc.link.stdout.txt"
    print_artifact_path_if_exists "SysyCC Link Stderr" \
        "${case_dir}/${case_name}.sysycc.link.stderr.txt"
    print_artifact_path_if_exists "SysyCC Run Exit" \
        "${case_dir}/${case_name}.sysycc.exit.txt"
    print_artifact_path_if_exists "SysyCC Run Stdout" \
        "${case_dir}/${case_name}.sysycc.stdout.txt"
    print_artifact_path_if_exists "SysyCC Run Stderr" \
        "${case_dir}/${case_name}.sysycc.stderr.txt"
    print_artifact_path_if_exists "Compare" \
        "${case_dir}/${case_name}.compare.txt"

    echo
}

run_binary_with_timeout() {
    local binary_path="$1"
    local stdin_file="$2"
    local stdout_file="$3"
    local stderr_file="$4"
    local status_file="$5"

    python3 - "${binary_path}" "${stdin_file}" "${stdout_file}" "${stderr_file}" "${status_file}" "${RUN_TIMEOUT_SECONDS}" <<'PY'
import pathlib
import subprocess
import sys

binary = pathlib.Path(sys.argv[1])
stdin_path = pathlib.Path(sys.argv[2])
stdout_path = pathlib.Path(sys.argv[3])
stderr_path = pathlib.Path(sys.argv[4])
status_path = pathlib.Path(sys.argv[5])
timeout_seconds = int(sys.argv[6])

with stdin_path.open("r", encoding="utf-8") as stdin_file, stdout_path.open(
    "w", encoding="utf-8"
) as stdout_file, stderr_path.open("w", encoding="utf-8") as stderr_file:
    try:
        result = subprocess.run(
            [str(binary)],
            stdin=stdin_file,
            stdout=stdout_file,
            stderr=stderr_file,
            timeout=timeout_seconds,
            check=False,
            text=True,
        )
        exit_code = result.returncode
    except subprocess.TimeoutExpired:
        exit_code = 124
        stderr_file.write(f"timed out after {timeout_seconds} seconds\n")

status_path.write_text(f"{exit_code}\n", encoding="utf-8")
print(exit_code)
PY
}

ensure_prerequisites() {
    if ! command -v clang >/dev/null 2>&1; then
        echo "missing required tool: clang" >&2
        exit 1
    fi

    if [[ ! -x "${SYSYCC_BIN}" ]]; then
        echo "missing SysyCC binary: ${SYSYCC_BIN}" >&2
        echo "build it first with: cmake -S . -B build && cmake --build build" >&2
        exit 1
    fi
}

compile_with_clang() {
    local case_file="$1"
    local binary_file="$2"
    local stdout_file="$3"
    local stderr_file="$4"
    local status_file="$5"

    if clang -w -O0 \
        -I "${CSMITH_RUNTIME_DIR}" \
        -I "${CSMITH_BUILD_RUNTIME_DIR}" \
        "${case_file}" \
        -o "${binary_file}" \
        >"${stdout_file}" 2>"${stderr_file}"; then
        printf '0\n' >"${status_file}"
        return 0
    fi

    printf '1\n' >"${status_file}"
    return 1
}

compile_with_sysycc() {
    local case_file="$1"
    local case_name="$2"
    local ll_file="$3"
    local stdout_file="$4"
    local stderr_file="$5"
    local status_file="$6"

    local intermediate_dir="${BUILD_DIR}/intermediate_results"
    local generated_ll_file="${intermediate_dir}/${case_name}.ll"

    if "${SYSYCC_BIN}" \
        -I "${CSMITH_RUNTIME_DIR}" \
        -I "${CSMITH_BUILD_RUNTIME_DIR}" \
        "${case_file}" \
        --dump-tokens \
        --dump-parse \
        --dump-ast \
        --dump-ir \
        >"${stdout_file}" 2>"${stderr_file}"; then
        printf '0\n' >"${status_file}"
        copy_sysycc_intermediates "${case_name}" "$(dirname "${ll_file}")"
        return 0
    fi

    printf '1\n' >"${status_file}"
    copy_sysycc_intermediates "${case_name}" "$(dirname "${ll_file}")"
    return 1
}

compile_sysycc_ir_with_clang() {
    local ll_file="$1"
    local binary_file="$2"
    local stdout_file="$3"
    local stderr_file="$4"
    local status_file="$5"
    local runtime_source="${PROJECT_ROOT}/tests/run/support/runtime_stub.c"
    local runtime_builtin_stub="${PROJECT_ROOT}/tests/run/support/runtime_builtin_stub.ll"

    if clang "${ll_file}" \
        "${runtime_source}" \
        "${runtime_builtin_stub}" \
        -fno-builtin \
        -o "${binary_file}" >"${stdout_file}" 2>"${stderr_file}"; then
        printf '0\n' >"${status_file}"
        return 0
    fi

    printf '1\n' >"${status_file}"
    return 1
}

compare_case_outputs() {
    local clang_stdout="$1"
    local clang_stderr="$2"
    local clang_exit="$3"
    local sysycc_stdout="$4"
    local sysycc_stderr="$5"
    local sysycc_exit="$6"
    local compare_file="$7"

    if diff -u "${clang_stdout}" "${sysycc_stdout}" >"${compare_file}"; then
        local clang_exit_code
        local sysycc_exit_code
        clang_exit_code="$(tr -d '\n' <"${clang_exit}")"
        sysycc_exit_code="$(tr -d '\n' <"${sysycc_exit}")"

        if [[ "${clang_exit_code}" == "${sysycc_exit_code}" ]] && cmp -s "${clang_stderr}" "${sysycc_stderr}"; then
            printf 'MATCH\n' >"${compare_file}"
            return 0
        fi

        {
            echo "stdout matched, but exit code or stderr differed"
            echo "clang exit: ${clang_exit_code}"
            echo "sysycc exit: ${sysycc_exit_code}"
        } >"${compare_file}"
        return 1
    fi

    return 1
}

run_case() {
    local case_id="$1"
    local case_dir="${SCRIPT_DIR}/${case_id}"
    local case_name="fuzz_${case_id}"
    local case_file="${case_dir}/${case_name}.c"
    local stdin_file="${case_dir}/${case_name}.input.txt"

    local clang_binary="${case_dir}/${case_name}.clang.out"
    local clang_compile_stdout="${case_dir}/${case_name}.clang.compile.stdout.txt"
    local clang_compile_stderr="${case_dir}/${case_name}.clang.compile.stderr.txt"
    local clang_compile_exit="${case_dir}/${case_name}.clang.compile.exit.txt"
    local clang_stdout="${case_dir}/${case_name}.clang.stdout.txt"
    local clang_stderr="${case_dir}/${case_name}.clang.stderr.txt"
    local clang_exit="${case_dir}/${case_name}.clang.exit.txt"

    local sysycc_ir="${case_dir}/${case_name}.ll"
    local sysycc_compile_stdout="${case_dir}/${case_name}.sysycc.compile.stdout.txt"
    local sysycc_compile_stderr="${case_dir}/${case_name}.sysycc.compile.stderr.txt"
    local sysycc_compile_exit="${case_dir}/${case_name}.sysycc.compile.exit.txt"
    local sysycc_binary="${case_dir}/${case_name}.sysycc.out"
    local sysycc_link_stdout="${case_dir}/${case_name}.sysycc.link.stdout.txt"
    local sysycc_link_stderr="${case_dir}/${case_name}.sysycc.link.stderr.txt"
    local sysycc_link_exit="${case_dir}/${case_name}.sysycc.link.exit.txt"
    local sysycc_stdout="${case_dir}/${case_name}.sysycc.stdout.txt"
    local sysycc_stderr="${case_dir}/${case_name}.sysycc.stderr.txt"
    local sysycc_exit="${case_dir}/${case_name}.sysycc.exit.txt"

    local compare_file="${case_dir}/${case_name}.compare.txt"
    local legacy_binary="${case_dir}/${case_name}.out"
    local legacy_stdout="${case_dir}/${case_name}.stdout.txt"
    local legacy_stderr="${case_dir}/${case_name}.stderr.txt"
    local legacy_exit="${case_dir}/${case_name}.exit.txt"

    if [[ ! -f "${case_file}" ]]; then
        append_result_row "${case_id}" "MISSING" "missing source file"
        print_case_intermediate_report "${case_id}" "MISSING" "missing source file"
        return
    fi

    if [[ ! -f "${stdin_file}" ]]; then
        : >"${stdin_file}"
    fi

    rm -f "${legacy_binary}" "${legacy_stdout}" "${legacy_stderr}" "${legacy_exit}"

    if ! compile_with_clang "${case_file}" "${clang_binary}" "${clang_compile_stdout}" "${clang_compile_stderr}" "${clang_compile_exit}"; then
        append_result_row "${case_id}" "CLANG_COMPILE_FAIL" "see ${case_name}.clang.compile.stderr.txt"
        print_case_intermediate_report "${case_id}" "CLANG_COMPILE_FAIL" "see ${case_name}.clang.compile.stderr.txt"
        return
    fi

    run_binary_with_timeout "${clang_binary}" "${stdin_file}" "${clang_stdout}" "${clang_stderr}" "${clang_exit}" >/dev/null

    if ! compile_with_sysycc "${case_file}" "${case_name}" "${sysycc_ir}" "${sysycc_compile_stdout}" "${sysycc_compile_stderr}" "${sysycc_compile_exit}"; then
        append_result_row "${case_id}" "SYSYCC_COMPILE_FAIL" "see ${case_name}.sysycc.compile.stderr.txt"
        print_case_intermediate_report "${case_id}" "SYSYCC_COMPILE_FAIL" "see ${case_name}.sysycc.compile.stderr.txt"
        return
    fi

    if [[ ! -f "${sysycc_ir}" ]]; then
        append_result_row "${case_id}" "SYSYCC_IR_MISSING" "expected ${case_name}.ll"
        print_case_intermediate_report "${case_id}" "SYSYCC_IR_MISSING" "expected ${case_name}.ll"
        return
    fi

    if ! compile_sysycc_ir_with_clang "${sysycc_ir}" "${sysycc_binary}" "${sysycc_link_stdout}" "${sysycc_link_stderr}" "${sysycc_link_exit}"; then
        append_result_row "${case_id}" "SYSYCC_LINK_FAIL" "see ${case_name}.sysycc.link.stderr.txt"
        print_case_intermediate_report "${case_id}" "SYSYCC_LINK_FAIL" "see ${case_name}.sysycc.link.stderr.txt"
        return
    fi

    run_binary_with_timeout "${sysycc_binary}" "${stdin_file}" "${sysycc_stdout}" "${sysycc_stderr}" "${sysycc_exit}" >/dev/null

    if compare_case_outputs "${clang_stdout}" "${clang_stderr}" "${clang_exit}" "${sysycc_stdout}" "${sysycc_stderr}" "${sysycc_exit}" "${compare_file}"; then
        append_result_row "${case_id}" "MATCH" "clang and sysycc outputs matched"
        print_case_intermediate_report "${case_id}" "MATCH" "clang and sysycc outputs matched"
    else
        append_result_row "${case_id}" "MISMATCH" "see ${case_name}.compare.txt"
        print_case_intermediate_report "${case_id}" "MISMATCH" "see ${case_name}.compare.txt"
    fi
}

if [[ $# -lt 1 ]]; then
    print_usage
    exit 1
fi

ensure_prerequisites
initialize_result_file

declare -a CASE_IDS=()

if [[ "$1" == "all" ]]; then
    while IFS= read -r case_dir; do
        CASE_IDS+=("$(basename "${case_dir}")")
    done < <(find "${SCRIPT_DIR}" -mindepth 1 -maxdepth 1 -type d -name '[0-9][0-9][0-9]' | sort)
else
    for raw_id in "$@"; do
        CASE_IDS+=("$(normalize_case_id "${raw_id}")")
    done
fi

if [[ "${#CASE_IDS[@]}" -eq 0 ]]; then
    echo "no fuzz case directories found under ${SCRIPT_DIR}" >&2
    exit 1
fi

for case_id in "${CASE_IDS[@]}"; do
    run_case "${case_id}"
done

cat "${RESULT_FILE}"
