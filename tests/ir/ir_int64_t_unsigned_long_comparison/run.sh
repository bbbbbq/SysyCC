#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_int64_t_unsigned_long_comparison.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
IR_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.ll"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ast --dump-ir

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"
assert_file_nonempty "${IR_FILE}"

grep -Eq '^  %t[0-9]+ = icmp ule i64 %t[0-9]+, 0$' "${IR_FILE}"
if rg -q '^  %t[0-9]+ = icmp sle i64 %t[0-9]+, 0$' "${IR_FILE}"; then
    echo "unexpected signed comparison for int64_t and unsigned long operands" >&2
    exit 1
fi

echo "verified: int64_t and unsigned long comparison lowers to unsigned LLVM compare"
