#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_conditional_expr.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
IR_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.ll"
CORE_IR_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.core-ir.txt"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-core-ir --dump-ir

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"
assert_file_nonempty "${IR_FILE}"
assert_file_nonempty "${CORE_IR_FILE}"

grep -Eq '^func @pick\(i32 %cond\) -> i32 \{$' "${CORE_IR_FILE}"
grep -Eq '^cond\.true[0-9]+:$' "${CORE_IR_FILE}"
grep -Eq '^cond\.false[0-9]+:$' "${CORE_IR_FILE}"
grep -Eq '^cond\.end[0-9]+:$' "${CORE_IR_FILE}"
grep -Eq '^  %t[0-9]+ = phi i32 \[ 11, %cond\.true[0-9]+ \], \[ 22, %cond\.false[0-9]+ \]$' "${CORE_IR_FILE}"

grep -Eq '^define i32 @pick\(i32 %cond\) \{$' "${IR_FILE}"
grep -Eq '^cond\.true[0-9]+:$' "${IR_FILE}"
grep -Eq '^cond\.false[0-9]+:$' "${IR_FILE}"
grep -Eq '^cond\.end[0-9]+:$' "${IR_FILE}"
grep -Eq '^  %t[0-9]+ = phi i32 \[ 11, %cond\.true[0-9]+ \], \[ 22, %cond\.false[0-9]+ \]$' "${IR_FILE}"

echo "verified: integer ternary expressions lower to phi-merged control-flow blocks"
