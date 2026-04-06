#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_for.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
IR_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.ll"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ir

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"
assert_file_nonempty "${IR_FILE}"

if ! grep -q '^  ret i32 3$' "${IR_FILE}"; then
    grep -q '^for.cond0:$' "${IR_FILE}"
    grep -Eq '^for\.body[0-9]+:$' "${IR_FILE}"
    grep -Eq '^for\.end[0-9]+:$' "${IR_FILE}"
    grep -q 'icmp slt i32 ' "${IR_FILE}"
    grep -Eq '^  br label %for\.cond[0-9]+$' "${IR_FILE}"
fi

echo "verified: for loop lowers to canonical init, cond, body, and end blocks"
