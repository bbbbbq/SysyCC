#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
RESULT_DIR="${PROJECT_ROOT}/build/intermediate_results"

assert_result_file() {
    local file_path="$1"

    if [[ ! -f "${file_path}" ]]; then
        echo "error: missing result file: ${file_path}" >&2
        exit 1
    fi

    if [[ ! -s "${file_path}" ]]; then
        echo "error: empty result file: ${file_path}" >&2
        exit 1
    fi
}

should_require_nonempty_artifacts() {
    local test_name="$1"

    case "${test_name}" in
        macro_literal_expansion_bug|function_macro_argument_literal_bug|include_cycle_bug|invalid_macro_name_bug|invalid_token_diagnostic|lexer_global_state_bug|lexer_parse_node_mode_guard|empty_token_stream_behavior|preprocess_dispatch_sentinel_bug)
            return 1
            ;;
        *)
            return 0
            ;;
    esac
}

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
    "${run_script}"

    if should_require_nonempty_artifacts "${test_name}"; then
        assert_result_file "${RESULT_DIR}/${test_name}.preprocessed.sy"
        assert_result_file "${RESULT_DIR}/${test_name}.tokens.txt"
        assert_result_file "${RESULT_DIR}/${test_name}.parse.txt"

        echo "    verified ${test_name}.preprocessed.sy"
        echo "    verified ${test_name}.tokens.txt"
        echo "    verified ${test_name}.parse.txt"
    else
        echo "    verified ${test_name} via dedicated run.sh assertions"
    fi
done
