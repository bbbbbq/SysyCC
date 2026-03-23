#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
RESULT_DIR="${PROJECT_ROOT}/build/intermediate_results"
SUMMARY_FILE="${PROJECT_ROOT}/build/test_result.md"
TEST_RESULT_DIR="${PROJECT_ROOT}/build/test_results"
TEST_LOG_DIR="${PROJECT_ROOT}/build/test_logs"

TEST_NAMES=()
TEST_STATUSES=()
TEST_DETAILS=()
RUN_SCRIPTS=()
DISPLAY_NAMES=()
STAGE_NAMES=()
CASE_NAMES=()
CASE_KEYS=()
CASE_LOG_FILES=()
JOB_PIDS=()
JOB_CASE_INDICES=()

source "${SCRIPT_DIR}/test_helpers.sh"

assert_result_file() {
    local file_path="$1"

    if [[ ! -f "${file_path}" ]]; then
        echo "missing result file: ${file_path}"
        return 1
    fi

    if [[ ! -s "${file_path}" ]]; then
        echo "empty result file: ${file_path}"
        return 1
    fi

    return 0
}

record_result() {
    local index="$1"
    local test_name="$2"
    local test_status="$3"
    local test_detail="$4"

    TEST_NAMES[index]="${test_name}"
    TEST_STATUSES[index]="${test_status}"
    TEST_DETAILS[index]="${test_detail}"
}

escape_table_cell() {
    local value="$1"
    value="${value//|/\\|}"
    value="${value//$'\n'/<br>}"
    printf '%s' "${value}"
}

write_summary_table() {
    local overall_status="$1"

    mkdir -p "$(dirname "${SUMMARY_FILE}")"
    {
        echo "# Test Result"
        echo
        echo "- Overall: ${overall_status}"
        echo
        echo "| Test | Status | Detail |"
        echo "| --- | --- | --- |"

        local index
        for index in "${!TEST_NAMES[@]}"; do
            printf '| %s | %s | %s |\n' \
                "$(escape_table_cell "${TEST_NAMES[index]}")" \
                "$(escape_table_cell "${TEST_STATUSES[index]}")" \
                "$(escape_table_cell "${TEST_DETAILS[index]}")"
        done
    } >"${SUMMARY_FILE}"

    echo
    echo "==> Result"
    cat "${SUMMARY_FILE}"
}

detect_default_jobs() {
    if command -v sysctl >/dev/null 2>&1; then
        local jobs
        jobs="$(sysctl -n hw.logicalcpu 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || true)"
        if [[ -n "${jobs}" ]]; then
            printf '%s\n' "${jobs}"
            return 0
        fi
    fi

    if command -v getconf >/dev/null 2>&1; then
        local jobs
        jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || true)"
        if [[ -n "${jobs}" ]]; then
            printf '%s\n' "${jobs}"
            return 0
        fi
    fi

    printf '4\n'
}

ensure_positive_integer() {
    local value="$1"
    local name="$2"

    if [[ ! "${value}" =~ ^[1-9][0-9]*$ ]]; then
        echo "error: ${name} must be a positive integer, got '${value}'" >&2
        exit 1
    fi
}

make_case_key() {
    local index="$1"
    local stage_name="$2"
    local test_name="$3"

    printf '%03d_%s_%s' "$((index + 1))" "${stage_name}" "${test_name}"
}

should_require_nonempty_artifacts() {
    local stage_name="$1"
    local test_name="$2"

    if [[ "${stage_name}" == "run" || "${stage_name}" == "fuzz" ]]; then
        return 1
    fi

    case "${test_name}" in
        macro_literal_expansion_bug|function_macro_argument_literal_bug|include_cycle_bug|invalid_macro_name_bug|define_invalid_parameter_name|define_duplicate_parameter_name|define_variadic_parameter_position|error_directive|error_directive_empty_payload|unmatched_else|unmatched_endif|duplicate_else|elif_after_else|include_missing_path|include_error_trace|include_next_missing|line_directive_invalid_number|defined_malformed|missing_endif|elifdef_missing_condition|line_directive_missing_number|line_directive_spaced_filename|line_directive_trailing_tokens|include_empty_path|has_include_malformed|invalid_token_diagnostic|lexer_global_state_bug|lexer_parse_node_mode_guard|empty_token_stream_behavior|line_directive_logical_location|line_directive_spaced_logical_location|preprocess_dispatch_sentinel_bug|macro_redefinition_conflict|default_dialect_registry|lexer_keyword_conflict_policy|lexer_keyword_runtime_classification|parser_feature_runtime_policy|ast_feature_runtime_policy|semantic_feature_runtime_policy|preprocess_feature_runtime_policy|dialect_registration_fail_fast|restrict_qualified_pointer_runtime_policy|pointer_nullability_runtime_policy|strict_c99_dialect_configuration|optional_dialect_pack_switches|cli_dialect_option_mapping|handler_registry_conflict_policy|parser_error_diagnostic|ast_unknown_guard|semantic_source_file|semantic_logical_source_file|semantic_undefined_identifier|semantic_redefinition|semantic_call_arity|semantic_call_type|semantic_call_target|semantic_return_type|semantic_arrow_type|semantic_dot_type|semantic_assign_type|semantic_assign_lvalue|semantic_member_field|semantic_condition_type|semantic_conditional_condition|semantic_index_base|semantic_index_type|semantic_unary_address|semantic_unary_deref|semantic_prefix_operand|semantic_postfix_operand|semantic_binary_arithmetic|semantic_binary_bitwise|semantic_binary_logical|semantic_break_context|semantic_continue_context|semantic_case_context|semantic_default_context|semantic_case_constant|semantic_duplicate_case|semantic_multiple_default|semantic_missing_return|semantic_array_dimension_constant|semantic_const_initializer_constant|semantic_const_pointer_call_reject|semantic_equality_pointer|semantic_goto_undefined_label|semantic_incompatible_pointer_assignment_warning|semantic_relational_pointer|semantic_unsupported_attribute|semantic_pointer_nullability_annotation|ir_pointer_nullability_erasure|ir_unsupported_function_error)
            return 1
            ;;
        *)
            return 0
            ;;
    esac
}

discover_tests() {
    while IFS= read -r -d '' run_script; do
        local test_dir
        local test_name
        local stage_name
        local display_name
        local case_index
        local case_key

        test_dir="$(dirname "${run_script}")"
        test_name="$(basename "${test_dir}")"
        stage_name="$(basename "$(dirname "${test_dir}")")"
        display_name="${stage_name}/${test_name}"
        case_index="${#RUN_SCRIPTS[@]}"
        case_key="$(make_case_key "${case_index}" "${stage_name}" "${test_name}")"

        RUN_SCRIPTS+=("${run_script}")
        DISPLAY_NAMES+=("${display_name}")
        STAGE_NAMES+=("${stage_name}")
        CASE_NAMES+=("${test_name}")
        CASE_KEYS+=("${case_key}")
        CASE_LOG_FILES+=("${TEST_LOG_DIR}/${case_key}.log")
    done < <(find "${SCRIPT_DIR}" -mindepth 3 -maxdepth 3 -type f -name run.sh -perm -111 -print0 | sort -z)
}

run_case() {
    local case_index="$1"
    local run_script="${RUN_SCRIPTS[case_index]}"
    local test_name="${CASE_NAMES[case_index]}"
    local stage_name="${STAGE_NAMES[case_index]}"
    local display_name="${DISPLAY_NAMES[case_index]}"
    local case_key="${CASE_KEYS[case_index]}"
    local status_file="${TEST_RESULT_DIR}/${case_key}.status"
    local detail_file="${TEST_RESULT_DIR}/${case_key}.detail"
    local log_file="${CASE_LOG_FILES[case_index]}"

    {
        echo "==> Running ${display_name}"

        if ! "${run_script}"; then
            printf 'FAIL\n' >"${status_file}"
            printf 'run.sh exited with non-zero status\n' >"${detail_file}"
            return 0
        fi

        if should_require_nonempty_artifacts "${stage_name}" "${test_name}"; then
            local detail_messages=()
            local test_failed=0
            local artifact_suffix
            local artifact_path
            local artifact_message

            for artifact_suffix in preprocessed.sy tokens.txt parse.txt; do
                artifact_path="${RESULT_DIR}/${test_name}.${artifact_suffix}"
                if artifact_message="$(assert_result_file "${artifact_path}")"; then
                    echo "    verified ${test_name}.${artifact_suffix}"
                    detail_messages+=("verified ${test_name}.${artifact_suffix}")
                else
                    echo "error: ${artifact_message}" >&2
                    detail_messages+=("${artifact_message}")
                    test_failed=1
                fi
            done

            if [[ "${test_failed}" -eq 0 ]]; then
                printf 'PASS\n' >"${status_file}"
                printf 'artifacts verified\n' >"${detail_file}"
            else
                printf 'FAIL\n' >"${status_file}"
                printf '%s\n' "$(printf '%s; ' "${detail_messages[@]}" | sed 's/; $//')" >"${detail_file}"
            fi
        else
            echo "    verified ${display_name} via dedicated run.sh assertions"
            printf 'PASS\n' >"${status_file}"
            printf 'verified via dedicated run.sh assertions\n' >"${detail_file}"
        fi
    } >"${log_file}" 2>&1
}

active_job_count() {
    local count=0
    local pid

    for pid in "${JOB_PIDS[@]-}"; do
        if [[ -n "${pid}" ]]; then
            count=$((count + 1))
        fi
    done

    printf '%s\n' "${count}"
}

finalize_case() {
    local case_index="$1"
    local display_name="${DISPLAY_NAMES[case_index]}"
    local case_key="${CASE_KEYS[case_index]}"
    local log_file="${CASE_LOG_FILES[case_index]}"
    local status_file="${TEST_RESULT_DIR}/${case_key}.status"
    local detail_file="${TEST_RESULT_DIR}/${case_key}.detail"
    local status="FAIL"
    local detail="test runner did not produce a result"

    if [[ -f "${status_file}" ]]; then
        status="$(<"${status_file}")"
    fi

    if [[ -f "${detail_file}" ]]; then
        detail="$(<"${detail_file}")"
    fi

    record_result "${case_index}" "${display_name}" "${status}" "${detail}"

    if [[ "${status}" == "PASS" ]]; then
        echo "[PASS] ${display_name}"
        return 0
    fi

    OVERALL_FAILURE=1
    echo "[FAIL] ${display_name}"
    echo "       ${detail}"
    if [[ -f "${log_file}" ]]; then
        echo "       log: ${log_file}"
    fi
}

collect_one_finished_job() {
    while true; do
        local job_slot
        local max_slots="${#JOB_PIDS[@]}"

        for ((job_slot = 0; job_slot < max_slots; ++job_slot)); do
            local pid="${JOB_PIDS[job_slot]-}"
            local case_index="${JOB_CASE_INDICES[job_slot]-}"

            if [[ -z "${pid}" ]]; then
                continue
            fi

            if kill -0 "${pid}" 2>/dev/null; then
                continue
            fi

            if ! wait "${pid}"; then
                :
            fi

            finalize_case "${case_index}"
            JOB_PIDS[job_slot]=""
            JOB_CASE_INDICES[job_slot]=""
            return 0
        done

        sleep 0.1
    done
}

OVERALL_FAILURE=0
DEFAULT_JOBS="$(detect_default_jobs)"
JOBS="${JOBS:-${DEFAULT_JOBS}}"

ensure_positive_integer "${JOBS}" "JOBS"

mkdir -p "${TEST_RESULT_DIR}" "${TEST_LOG_DIR}"

discover_tests

if [[ "${#RUN_SCRIPTS[@]}" -eq 0 ]]; then
    echo "no executable run.sh test cases found under ${SCRIPT_DIR}" >&2
    exit 1
fi

export SYSYCC_TEST_DISABLE_NINJA="${SYSYCC_TEST_DISABLE_NINJA:-0}"
export SYSYCC_TEST_DISABLE_CCACHE="${SYSYCC_TEST_DISABLE_CCACHE:-0}"
echo "==> Building project once before running ${#RUN_SCRIPTS[@]} tests"
export SYSYCC_TEST_BUILD_JOBS="${SYSYCC_TEST_BUILD_JOBS:-${JOBS}}"
build_project "${PROJECT_ROOT}" "${PROJECT_ROOT}/build"
export SYSYCC_TEST_SKIP_BUILD=1
echo "==> Running tests with JOBS=${JOBS}"

for case_index in "${!RUN_SCRIPTS[@]}"; do
    while [[ "$(active_job_count)" -ge "${JOBS}" ]]; do
        collect_one_finished_job
    done

    run_case "${case_index}" &
    JOB_PIDS+=("$!")
    JOB_CASE_INDICES+=("${case_index}")
done

while [[ "$(active_job_count)" -gt 0 ]]; do
    collect_one_finished_job
done

if [[ "${OVERALL_FAILURE}" -eq 0 ]]; then
    write_summary_table "PASS"
    exit 0
fi

write_summary_table "FAIL"
exit 1
