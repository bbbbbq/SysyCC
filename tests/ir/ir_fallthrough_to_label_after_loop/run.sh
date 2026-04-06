#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_fallthrough_to_label_after_loop.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
IR_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.ll"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ast --dump-ir

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"
assert_file_nonempty "${IR_FILE}"

if ! grep -q '^  ret i32 1$' "${IR_FILE}"; then
    grep -Eq '^  br i1 %t[0-9]+(\.raw)?, label %for\.step[0-9]+, label %for\.end[0-9]+$' "${IR_FILE}"
    grep -Eq '^for\.end[0-9]+:$' "${IR_FILE}"
    grep -Eq '^  ret i32 %t[0-9]+$' "${IR_FILE}"
fi

echo "verified: fallthrough after a loop can fold the unused label block into the loop exit"
