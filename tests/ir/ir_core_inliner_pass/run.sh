#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_core_inliner_pass.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
CORE_IR_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.core-ir.txt"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-core-ir

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"
assert_file_nonempty "${CORE_IR_FILE}"

if grep -Eq 'call i32 @helper' "${CORE_IR_FILE}"; then
    echo "expected helper call to be inlined away" >&2
    exit 1
fi

if grep -Eq 'call i32 @too_complex' "${CORE_IR_FILE}"; then
    echo "expected structured multi-block call to be inlined away" >&2
    exit 1
fi

grep -Eq '^func @too_complex\(i32 %x\) -> i32 \{$' "${CORE_IR_FILE}"

echo "verified: inliner consumes simple and structured multi-block direct calls while preserving externally visible definitions"
