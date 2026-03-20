#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/preprocess_nested_conditionals.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"

PREPROCESSED_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.preprocessed.sy"
grep -q '^int selected_outer() {$' "${PREPROCESSED_FILE}"
grep -q '^    return 200;$' "${PREPROCESSED_FILE}"
if grep -q '100\|300\|400' "${PREPROCESSED_FILE}"; then
    echo "unexpected nested conditional branch remained in preprocessed output" >&2
    exit 1
fi

echo "verified: nested conditional preprocessing selects only the active branch"
