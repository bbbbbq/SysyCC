#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_switch.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
IR_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.ll"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ir

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"
assert_file_nonempty "${IR_FILE}"

grep -q '^switch.case0:$' "${IR_FILE}"
grep -q '^switch.case1:$' "${IR_FILE}"
grep -q '^switch.default2:$' "${IR_FILE}"
grep -q '^switch.end3:$' "${IR_FILE}"
grep -q '^switch.test4:$' "${IR_FILE}"
grep -q '^switch.test5:$' "${IR_FILE}"
grep -q 'icmp eq i32 ' "${IR_FILE}"
grep -q 'br i1 ' "${IR_FILE}"
grep -q '^  store i32 20, ptr %y.addr$' "${IR_FILE}"
grep -q '^  br label %switch.end3$' "${IR_FILE}"
grep -q '^  ret i32 ' "${IR_FILE}"

echo "verified: switch/case/default lowers to compare chain and case blocks"
