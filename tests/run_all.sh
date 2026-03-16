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

for script in "${SCRIPT_DIR}"/*.sh; do
    if [[ "${script}" == "${SCRIPT_DIR}/run_all.sh" ]]; then
        continue
    fi

    test_name="$(basename "${script}" .sh)"

    echo "==> Running ${test_name}"
    "${script}"

    assert_result_file "${RESULT_DIR}/${test_name}.preprocessed.sy"
    assert_result_file "${RESULT_DIR}/${test_name}.tokens.txt"
    assert_result_file "${RESULT_DIR}/${test_name}.parse.txt"

    echo "    verified ${test_name}.preprocessed.sy"
    echo "    verified ${test_name}.tokens.txt"
    echo "    verified ${test_name}.parse.txt"
done
