#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_unsigned_bit_field_integer_promotion.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
IR_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.ll"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ast --dump-ir

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"
assert_file_nonempty "${IR_FILE}"

grep -Eq '^  %t[0-9]+\.raw = icmp sle i32 %t[0-9]+, %t[0-9]+$' "${IR_FILE}"
if rg -q '^  %t[0-9]+\.raw = icmp ule i32 %t[0-9]+, %t[0-9]+$' "${IR_FILE}"; then
    echo "unexpected unsigned compare for promoted unsigned bit-field operand" >&2
    exit 1
fi

echo "verified: unsigned bit-field values that fit in int lower through signed comparison"
