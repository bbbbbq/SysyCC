#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
RESULT_DIR="${PROJECT_ROOT}/build/intermediate_results"
SUMMARY_FILE="${PROJECT_ROOT}/build/test_result.md"

TEST_NAMES=()
TEST_STATUSES=()
TEST_DETAILS=()

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
    local test_name="$1"
    local test_status="$2"
    local test_detail="$3"

    TEST_NAMES+=("${test_name}")
    TEST_STATUSES+=("${test_status}")
    TEST_DETAILS+=("${test_detail}")
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

should_require_nonempty_artifacts() {
    local test_name="$1"

    case "${test_name}" in
        macro_literal_expansion_bug|function_macro_argument_literal_bug|include_cycle_bug|invalid_macro_name_bug|invalid_token_diagnostic|lexer_global_state_bug|lexer_parse_node_mode_guard|empty_token_stream_behavior|preprocess_dispatch_sentinel_bug|ast_unknown_guard|semantic_undefined_identifier|semantic_redefinition|semantic_call_arity|semantic_call_type|semantic_call_target|semantic_return_type|semantic_arrow_type|semantic_assign_type|semantic_assign_lvalue|semantic_member_field|semantic_condition_type|semantic_index_base|semantic_index_type|semantic_unary_address|semantic_unary_deref|semantic_prefix_operand|semantic_postfix_operand|semantic_binary_arithmetic|semantic_binary_bitwise|semantic_binary_logical|semantic_break_context|semantic_continue_context|semantic_case_context|semantic_default_context|semantic_case_constant|semantic_duplicate_case|semantic_multiple_default|semantic_missing_return|semantic_array_dimension_constant|semantic_equality_pointer|semantic_relational_pointer)
            return 1
            ;;
        *)
            return 0
            ;;
    esac
}

OVERALL_FAILURE=0

for test_dir in "${SCRIPT_DIR}"/*/; do
    if [[ ! -d "${test_dir}" ]]; then
        continue
    fi

    run_script="${test_dir}run.sh"
    if [[ ! -x "${run_script}" ]]; then
        continue
    fi

    test_name="$(basename "${test_dir}")"

    echo "==> Running ${test_name}"
    if ! "${run_script}"; then
        record_result "${test_name}" "FAIL" "run.sh exited with non-zero status"
        OVERALL_FAILURE=1
        continue
    fi

    if should_require_nonempty_artifacts "${test_name}"; then
        detail_messages=()
        test_failed=0

        for artifact_suffix in preprocessed.sy tokens.txt parse.txt; do
            artifact_path="${RESULT_DIR}/${test_name}.${artifact_suffix}"
            if artifact_message="$(assert_result_file "${artifact_path}")"; then
                echo "    verified ${test_name}.${artifact_suffix}"
                detail_messages+=("verified ${test_name}.${artifact_suffix}")
            else
                echo "error: ${artifact_message}" >&2
                detail_messages+=("${artifact_message}")
                OVERALL_FAILURE=1
                test_failed=1
            fi
        done

        if [[ "${test_failed}" -eq 0 ]]; then
            record_result "${test_name}" "PASS" "artifacts verified"
        else
            record_result "${test_name}" "FAIL" "$(printf '%s; ' "${detail_messages[@]}" | sed 's/; $//')"
        fi
    else
        echo "    verified ${test_name} via dedicated run.sh assertions"
        record_result "${test_name}" "PASS" "verified via dedicated run.sh assertions"
    fi
done

if [[ "${OVERALL_FAILURE}" -eq 0 ]]; then
    write_summary_table "PASS"
    exit 0
fi

write_summary_table "FAIL"
exit 1
