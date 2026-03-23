#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_union_assignment_call_argument.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
IR_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.ll"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ir

assert_file_nonempty "${IR_FILE}"
grep -Eq '^define (internal )?i32 @make_box\(i32 %value\) \{$' "${IR_FILE}"
grep -Eq '^  %t[0-9]+ = call i32 @make_box\(i32 7\)$' "${IR_FILE}"
grep -Eq '^  store i32 %t[0-9]+, ptr %box\.addr[0-9]*$' "${IR_FILE}"
grep -Eq '^  %t[0-9]+ = call i32 @read_box\(i32 %t[0-9]+\)$' "${IR_FILE}"

echo "verified: scalar-sized union assignment expressions can flow directly into call arguments"
