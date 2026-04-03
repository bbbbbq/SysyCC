#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
SYSYCC_BIN="${BUILD_DIR}/SysyCC"
CSMITH_RUNTIME_DIR="${PROJECT_ROOT}/tools/csmith/runtime"
CSMITH_BUILD_RUNTIME_DIR="${PROJECT_ROOT}/tools/csmith/build/runtime"
RUN_TIMEOUT_SECONDS="${RUN_TIMEOUT_SECONDS:-10}"
CASE_ROOT="${SYSYCC_FUZZ_CASE_ROOT:-${SCRIPT_DIR}}"
RESULT_FILE="${SYSYCC_FUZZ_RESULT_FILE:-${CASE_ROOT}/result.md}"
PRINT_INTERMEDIATE_MODE="${PRINT_INTERMEDIATE_MODE:-never}"
SYSYCC_FUZZ_CAPTURE_INTERMEDIATES="${SYSYCC_FUZZ_CAPTURE_INTERMEDIATES:-none}"
RUN_STARTED_AT="$(date '+%Y-%m-%d %H:%M:%S %Z')"

print_usage() {
    echo "usage: $0 [all | <case-id> [<case-id> ...]]" >&2
    echo "examples:" >&2
    echo "  $0" >&2
    echo "  $0 all" >&2
    echo "  $0 001" >&2
    echo "  $0 001 004 007" >&2
    echo >&2
    echo "environment:" >&2
    echo "  PRINT_INTERMEDIATE_MODE=never|failure|always  (default: never)" >&2
    echo "  SYSYCC_FUZZ_CAPTURE_INTERMEDIATES=none|full  (default: none)" >&2
    echo "  RUN_FUZZ_JOBS=<n>  override automatic parallel job detection" >&2
    echo "  SYSYCC_FUZZ_RESULT_FILE=<path>  write markdown report to a custom file" >&2
}

normalize_case_id() {
    local raw_id="$1"
    local numeric_id=0
    local case_id=""

    if ! [[ "${raw_id}" =~ ^[0-9]+$ ]]; then
        echo "invalid case id: ${raw_id}" >&2
        exit 1
    fi

    if [[ -d "${CASE_ROOT}/${raw_id}" ]]; then
        printf '%s\n' "${raw_id}"
        return
    fi

    numeric_id=$((10#${raw_id}))
    while IFS= read -r case_id; do
        if [[ $((10#${case_id})) -eq "${numeric_id}" ]]; then
            printf '%s\n' "${case_id}"
            return
        fi
    done < <(list_available_case_ids)

    printf "%03d\n" "${numeric_id}"
}

list_available_case_ids() {
    local case_dir=""
    local case_id=""

    while IFS= read -r case_dir; do
        case_id="$(basename "${case_dir}")"
        if [[ "${case_id}" =~ ^[0-9]+$ ]]; then
            printf '%s\n' "${case_id}"
        fi
    done < <(find "${CASE_ROOT}" -mindepth 1 -maxdepth 1 -type d)
}

collect_requested_case_ids() {
    CASE_IDS=()

    if [[ $# -eq 0 || "${1:-}" == "all" ]]; then
        while IFS= read -r case_id; do
            CASE_IDS+=("${case_id}")
        done < <(list_available_case_ids | sort -n)
        return
    fi

    local raw_id=""
    for raw_id in "$@"; do
        CASE_IDS+=("$(normalize_case_id "${raw_id}")")
    done
}

detect_parallel_jobs() {
    local detected_jobs="${RUN_FUZZ_JOBS:-}"

    if [[ -z "${detected_jobs}" ]] && command -v sysctl >/dev/null 2>&1; then
        detected_jobs="$(sysctl -n hw.logicalcpu 2>/dev/null || true)"
    fi

    if [[ -z "${detected_jobs}" ]] && command -v nproc >/dev/null 2>&1; then
        detected_jobs="$(nproc 2>/dev/null || true)"
    fi

    if [[ -z "${detected_jobs}" ]] && command -v getconf >/dev/null 2>&1; then
        detected_jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || true)"
    fi

    if ! [[ "${detected_jobs}" =~ ^[1-9][0-9]*$ ]]; then
        detected_jobs=1
    fi

    printf '%s\n' "${detected_jobs}"
}

append_result_row() {
    local case_id="$1"
    local status="$2"
    local detail="$3"
    local destination_file="${SYSYCC_FUZZ_RESULT_ROW_FILE:-${RESULT_FILE}}"
    if [[ -n "${SYSYCC_FUZZ_RESULT_ROW_FILE:-}" ]]; then
        printf '| %s | %s | %s |\n' "${case_id}" "${status}" "${detail}" >"${destination_file}"
    else
        printf '| %s | %s | %s |\n' "${case_id}" "${status}" "${detail}" >>"${destination_file}"
    fi
}

initialize_result_file() {
    mkdir -p "$(dirname "${RESULT_FILE}")"
    cat >"${RESULT_FILE}" <<'EOF'
# Fuzz Result

| Case | Status | Detail |
| --- | --- | --- |
EOF
}

append_result_metadata() {
    {
        echo
        echo "Generated at: ${RUN_STARTED_AT}"
        echo
        echo "- Case root: ${CASE_ROOT}"
        echo "- Requested cases: ${TOTAL_CASE_COUNT}"
        echo "- Parallel jobs: ${PARALLEL_JOBS}"
        echo "- Markdown report: ${RESULT_FILE}"
    } >>"${RESULT_FILE}"
}

append_result_summary() {
    local summary_file=""
    summary_file="$(mktemp)"

    awk '
        /^\| [^ ]+ \| [^ ]+ \|/ {
            if ($0 ~ /^\| Case \|/ || $0 ~ /^\| --- \|/) {
                next;
            }

            line = $0;
            gsub(/^\| /, "", line);
            split(line, parts, " \\| ");
            status = parts[2];
            total += 1;
            counts[status] += 1;
        }
        END {
            print "## Summary";
            print "";
            printf("- Total cases: %d\n", total);
            for (status in counts) {
                printf("- %s: %d\n", status, counts[status]);
            }
        }
    ' "${RESULT_FILE}" >"${summary_file}"

    {
        echo
        cat "${summary_file}"
    } >>"${RESULT_FILE}"

    rm -f "${summary_file}"
}

copy_optional_intermediate_file() {
    local source_dir="$1"
    local case_name="$2"
    local suffix="$3"
    local destination_file="$4"
    local source_file="${source_dir}/${case_name}.${suffix}"

    if [[ -f "${source_file}" ]]; then
        cp "${source_file}" "${destination_file}"
    fi
}

copy_sysycc_intermediates() {
    local source_dir="$1"
    local case_name="$2"
    local case_dir="$3"

    copy_optional_intermediate_file "${source_dir}" "${case_name}" "preprocessed.sy" \
        "${case_dir}/${case_name}.preprocessed.sy"
    copy_optional_intermediate_file "${source_dir}" "${case_name}" "tokens.txt" \
        "${case_dir}/${case_name}.tokens.txt"
    copy_optional_intermediate_file "${source_dir}" "${case_name}" "parse.txt" \
        "${case_dir}/${case_name}.parse.txt"
    copy_optional_intermediate_file "${source_dir}" "${case_name}" "ast.txt" \
        "${case_dir}/${case_name}.ast.txt"
    copy_optional_intermediate_file "${source_dir}" "${case_name}" "ll" \
        "${case_dir}/${case_name}.ll"
}

cleanup_case_intermediate_files() {
    local case_name="$1"
    local case_dir="$2"

    rm -f \
        "${case_dir}/${case_name}.preprocessed.sy" \
        "${case_dir}/${case_name}.tokens.txt" \
        "${case_dir}/${case_name}.parse.txt" \
        "${case_dir}/${case_name}.ast.txt" \
        "${case_dir}/${case_name}.ll"
}

get_sysycc_dump_mode() {
    case "${SYSYCC_FUZZ_CAPTURE_INTERMEDIATES}" in
    none | full)
        printf '%s\n' "${SYSYCC_FUZZ_CAPTURE_INTERMEDIATES}"
        ;;
    *)
        echo "invalid SYSYCC_FUZZ_CAPTURE_INTERMEDIATES: ${SYSYCC_FUZZ_CAPTURE_INTERMEDIATES}" >&2
        exit 1
        ;;
    esac
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
    local case_dir="${CASE_ROOT}/${case_id}"
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
    local case_dir="$4"
    local stdout_file="$5"
    local stderr_file="$6"
    local status_file="$7"

    local intermediate_dir="${BUILD_DIR}/intermediate_results"
    local sysycc_worker_root="${SYSYCC_FUZZ_WORKER_ROOT:-${TMPDIR:-/tmp}}"
    local invoke_dir=""
    local generated_intermediate_dir=""
    local generated_ll_file=""
    local dump_mode=""
    local -a dump_flags=("--dump-ir")

    dump_mode="$(get_sysycc_dump_mode)"
    if [[ "${dump_mode}" == "full" ]]; then
        dump_flags+=(
            "--dump-tokens"
            "--dump-parse"
            "--dump-ast"
        )
    fi

    invoke_dir="$(mktemp -d "${sysycc_worker_root}/${case_name}.sysycc.XXXXXX")"
    generated_intermediate_dir="${invoke_dir}/build/intermediate_results"
    generated_ll_file="${generated_intermediate_dir}/${case_name}.ll"

    if (
        cd "${invoke_dir}" &&
        "${SYSYCC_BIN}" \
            -I "${CSMITH_RUNTIME_DIR}" \
            -I "${CSMITH_BUILD_RUNTIME_DIR}" \
            "${case_file}" \
            "${dump_flags[@]}"
    ) >"${stdout_file}" 2>"${stderr_file}"; then
        printf '0\n' >"${status_file}"
        if [[ -f "${generated_ll_file}" ]]; then
            cp "${generated_ll_file}" "${ll_file}"
        fi
        if [[ "${dump_mode}" == "full" ]]; then
            copy_sysycc_intermediates "${generated_intermediate_dir}" "${case_name}" "${case_dir}"
        fi
        rm -rf "${invoke_dir}"
        return 0
    fi

    printf '1\n' >"${status_file}"
    if [[ -f "${generated_ll_file}" ]]; then
        cp "${generated_ll_file}" "${ll_file}"
    fi
    if [[ "${dump_mode}" == "full" ]]; then
        copy_sysycc_intermediates "${generated_intermediate_dir}" "${case_name}" "${case_dir}"
    fi
    rm -rf "${invoke_dir}"
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
    local case_dir="${CASE_ROOT}/${case_id}"
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

    local worker_root="${SYSYCC_FUZZ_WORKER_ROOT:-${TMPDIR:-/tmp}}"
    local sysycc_ir=""
    sysycc_ir="$(mktemp "${worker_root}/${case_name}.XXXXXX.ll")"
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
        rm -f "${sysycc_ir}"
        return
    fi

    if [[ ! -f "${stdin_file}" ]]; then
        : >"${stdin_file}"
    fi

    cleanup_case_intermediate_files "${case_name}" "${case_dir}"
    rm -f "${legacy_binary}" "${legacy_stdout}" "${legacy_stderr}" "${legacy_exit}"

    if ! compile_with_clang "${case_file}" "${clang_binary}" "${clang_compile_stdout}" "${clang_compile_stderr}" "${clang_compile_exit}"; then
        append_result_row "${case_id}" "CLANG_COMPILE_FAIL" "see ${case_name}.clang.compile.stderr.txt"
        print_case_intermediate_report "${case_id}" "CLANG_COMPILE_FAIL" "see ${case_name}.clang.compile.stderr.txt"
        rm -f "${sysycc_ir}"
        return
    fi

    run_binary_with_timeout "${clang_binary}" "${stdin_file}" "${clang_stdout}" "${clang_stderr}" "${clang_exit}" >/dev/null

    if ! compile_with_sysycc "${case_file}" "${case_name}" "${sysycc_ir}" "${case_dir}" "${sysycc_compile_stdout}" "${sysycc_compile_stderr}" "${sysycc_compile_exit}"; then
        append_result_row "${case_id}" "SYSYCC_COMPILE_FAIL" "see ${case_name}.sysycc.compile.stderr.txt"
        print_case_intermediate_report "${case_id}" "SYSYCC_COMPILE_FAIL" "see ${case_name}.sysycc.compile.stderr.txt"
        rm -f "${sysycc_ir}"
        return
    fi

    if [[ ! -f "${sysycc_ir}" ]]; then
        append_result_row "${case_id}" "SYSYCC_IR_MISSING" "expected ${case_name}.ll"
        print_case_intermediate_report "${case_id}" "SYSYCC_IR_MISSING" "expected ${case_name}.ll"
        rm -f "${sysycc_ir}"
        return
    fi

    if ! compile_sysycc_ir_with_clang "${sysycc_ir}" "${sysycc_binary}" "${sysycc_link_stdout}" "${sysycc_link_stderr}" "${sysycc_link_exit}"; then
        append_result_row "${case_id}" "SYSYCC_LINK_FAIL" "see ${case_name}.sysycc.link.stderr.txt"
        print_case_intermediate_report "${case_id}" "SYSYCC_LINK_FAIL" "see ${case_name}.sysycc.link.stderr.txt"
        rm -f "${sysycc_ir}"
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

    rm -f "${sysycc_ir}"
}

launch_case_worker() {
    local case_id="$1"
    local worker_root="$2"
    local report_file="${worker_root}/${case_id}.report.txt"
    local stderr_file="${worker_root}/${case_id}.driver.stderr.txt"
    local row_file="${worker_root}/${case_id}.row.txt"

    SYSYCC_FUZZ_RESULT_ROW_FILE="${row_file}" \
        SYSYCC_FUZZ_WORKER_ROOT="${worker_root}" \
        bash "${BASH_SOURCE[0]}" --run-case-internal "${case_id}" \
        >"${report_file}" 2>"${stderr_file}" &
    LAST_LAUNCHED_WORKER_PID="$!"
}

print_progress_update() {
    local case_id="$1"

    COMPLETED_CASE_COUNT=$((COMPLETED_CASE_COUNT + 1))
    printf '==> Progress: %d/%d completed (last: %s)\n' \
        "${COMPLETED_CASE_COUNT}" "${TOTAL_CASE_COUNT}" "${case_id}"
}

wait_for_available_slot() {
    local max_jobs="$1"

    while [[ "${#ACTIVE_PIDS[@]}" -ge "${max_jobs}" ]]; do
        local -a remaining_pids=()
        local -a remaining_case_ids=()
        local worker_index=""
        local worker_pid=""
        local case_id=""

        for worker_index in "${!ACTIVE_PIDS[@]}"; do
            worker_pid="${ACTIVE_PIDS[worker_index]}"
            case_id="${ACTIVE_CASE_IDS[worker_index]}"
            if kill -0 "${worker_pid}" 2>/dev/null; then
                remaining_pids+=("${worker_pid}")
                remaining_case_ids+=("${case_id}")
                continue
            fi

            wait "${worker_pid}" || true
            print_progress_update "${case_id}"
        done

        if [[ "${#remaining_pids[@]}" -gt 0 ]]; then
            ACTIVE_PIDS=("${remaining_pids[@]}")
            ACTIVE_CASE_IDS=("${remaining_case_ids[@]}")
        else
            ACTIVE_PIDS=()
            ACTIVE_CASE_IDS=()
        fi
        if [[ "${#ACTIVE_PIDS[@]}" -ge "${max_jobs}" ]]; then
            sleep 0.1
        fi
    done
}

wait_for_all_workers() {
    while [[ "${#ACTIVE_PIDS[@]}" -gt 0 ]]; do
        wait_for_available_slot 1
    done
}

run_requested_cases_in_parallel() {
    local worker_root="$1"
    local parallel_jobs="$2"
    shift 2
    local -a requested_case_ids=("$@")
    local case_id=""
    local worker_pid=""

    ACTIVE_PIDS=()
    ACTIVE_CASE_IDS=()

    for case_id in "${requested_case_ids[@]}"; do
        wait_for_available_slot "${parallel_jobs}"
        launch_case_worker "${case_id}" "${worker_root}"
        worker_pid="${LAST_LAUNCHED_WORKER_PID}"
        ACTIVE_PIDS+=("${worker_pid}")
        ACTIVE_CASE_IDS+=("${case_id}")
    done

    wait_for_all_workers
}

emit_parallel_results() {
    local worker_root="$1"
    shift
    local -a requested_case_ids=("$@")
    local case_id=""

    for case_id in "${requested_case_ids[@]}"; do
        local row_file="${worker_root}/${case_id}.row.txt"
        local report_file="${worker_root}/${case_id}.report.txt"
        local stderr_file="${worker_root}/${case_id}.driver.stderr.txt"

        if [[ -f "${stderr_file}" && -s "${stderr_file}" ]]; then
            cat "${stderr_file}" >&2
        fi

        if [[ -f "${row_file}" ]]; then
            cat "${row_file}" >>"${RESULT_FILE}"
        else
            append_result_row "${case_id}" "SCRIPT_FAIL" \
                "internal worker exited unexpectedly"
        fi

        if [[ -f "${report_file}" && -s "${report_file}" ]]; then
            cat "${report_file}"
        fi
    done
}

if [[ "${1:-}" == "--run-case-internal" ]]; then
    if [[ $# -ne 2 ]]; then
        echo "usage: $0 --run-case-internal <case-id>" >&2
        exit 1
    fi

    ensure_prerequisites
    run_case "$(normalize_case_id "$2")"
    exit 0
fi

if [[ "${1:-}" == "--list-case-ids-internal" ]]; then
    shift
    declare -a CASE_IDS=()
    collect_requested_case_ids "$@"
    if [[ "${#CASE_IDS[@]}" -gt 0 ]]; then
        printf '%s\n' "${CASE_IDS[@]}"
    fi
    exit 0
fi

declare -a CASE_IDS=()
collect_requested_case_ids "$@"

if [[ "${#CASE_IDS[@]}" -eq 0 ]]; then
    echo "no fuzz case directories found under ${CASE_ROOT}" >&2
    exit 1
fi

PARALLEL_JOBS="$(detect_parallel_jobs)"
WORKER_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/run_csmith_cases.XXXXXX")"
TOTAL_CASE_COUNT="${#CASE_IDS[@]}"
COMPLETED_CASE_COUNT=0
trap 'rm -rf "${WORKER_ROOT}"' EXIT

ensure_prerequisites
initialize_result_file
append_result_metadata

echo "==> Running ${#CASE_IDS[@]} fuzz cases with ${PARALLEL_JOBS} parallel jobs"

run_requested_cases_in_parallel "${WORKER_ROOT}" "${PARALLEL_JOBS}" "${CASE_IDS[@]}"
emit_parallel_results "${WORKER_ROOT}" "${CASE_IDS[@]}"
append_result_summary

echo "==> Markdown report written to ${RESULT_FILE}"
cat "${RESULT_FILE}"
