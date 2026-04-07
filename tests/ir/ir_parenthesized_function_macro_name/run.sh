#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_parenthesized_function_macro_name.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
IR_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.ll"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ir

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"
assert_file_nonempty "${IR_FILE}"
grep -Eq '^static int \(safe_add\)\(int x, int y\) \{$' \
    "${BUILD_DIR}/intermediate_results/${TEST_NAME}.preprocessed.sy"
grep -Eq '^define i32 @main\(\) \{$' "${IR_FILE}"
grep -Eq '^  ret i32 3$' "${IR_FILE}"

echo "verified: parenthesized macro-expanded function names preprocess and lower correctly through optimization"
