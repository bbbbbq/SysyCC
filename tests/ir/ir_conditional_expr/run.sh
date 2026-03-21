#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_conditional_expr.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
IR_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.ll"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ir

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"
assert_file_nonempty "${IR_FILE}"

grep -Eq '^cond\.true[0-9]+:$' "${IR_FILE}"
grep -Eq '^cond\.false[0-9]+:$' "${IR_FILE}"
grep -Eq '^cond\.end[0-9]+:$' "${IR_FILE}"
grep -Eq 'store i32 11, ptr %addr\.addr[0-9]*' "${IR_FILE}"
grep -Eq 'store i32 22, ptr %addr\.addr[0-9]*' "${IR_FILE}"
grep -Eq 'br i1 %t[0-9]+, label %cond\.true[0-9]+, label %cond\.false[0-9]+' "${IR_FILE}"

echo "verified: integer ternary expressions lower to conditional control-flow blocks"
