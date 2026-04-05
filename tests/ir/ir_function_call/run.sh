#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_function_call.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
IR_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.ll"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ir

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"
assert_file_nonempty "${IR_FILE}"

grep -q '^define i32 @add(i32 %lhs, i32 %rhs) {$' "${IR_FILE}"
grep -Eq '^  %t[0-9]+ = add i32 (%lhs|%t[0-9]+), (%rhs|%t[0-9]+)$' "${IR_FILE}"
grep -q '^define i32 @main() {$' "${IR_FILE}"
grep -Eq '^  %t[0-9]+ = call i32 @add\(i32 1, i32 2\)$' "${IR_FILE}"
grep -Eq '^  ret i32 %t[0-9]+$' "${IR_FILE}"

echo "verified: function parameters and calls lower to LLVM IR"
