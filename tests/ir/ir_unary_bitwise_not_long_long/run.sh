#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_unary_bitwise_not_long_long.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
IR_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.ll"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ast --dump-ir

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"
assert_file_nonempty "${IR_FILE}"

grep -Eq '^  %t[0-9]+ = sext i32 -1 to i64$' "${IR_FILE}"
grep -Eq '^  %t[0-9]+ = xor i64 9223372036854775807, %t[0-9]+$' "${IR_FILE}"
if rg -q '^  %t[0-9]+ = trunc i64 9223372036854775807 to i32$' "${IR_FILE}"; then
    echo "unexpected i32 truncation for unary long long bitwise not" >&2
    exit 1
fi

echo "verified: unary bitwise not keeps long long width through IR lowering"
