#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_function_pointer_call.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
IR_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.ll"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ir

assert_file_nonempty "${IR_FILE}"
grep -Eq 'store ptr @inc, ptr %fn\.addr[0-9]*' "${IR_FILE}"
grep -Eq '%t[0-9]+ = call i32 (%fn|%t[0-9]+)\(i32 (%value|%t[0-9]+)\)' "${IR_FILE}"
grep -Eq '%t[0-9]+ = call i32 @apply\(ptr @inc, i32 4\)' "${IR_FILE}"

echo "verified: ir lowers function designator decay and indirect calls"
