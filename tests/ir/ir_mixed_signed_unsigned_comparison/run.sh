#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_mixed_signed_unsigned_comparison.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
IR_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.ll"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ast --dump-ir

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"
assert_file_nonempty "${IR_FILE}"

grep -Eq '^  %t[0-9]+\.raw = icmp ult i32 %t[0-9]+, %t[0-9]+$' "${IR_FILE}"
if rg -q '^  %t[0-9]+\.raw = icmp slt i32 %t[0-9]+, %t[0-9]+$' "${IR_FILE}"; then
    echo "unexpected signed comparison for mixed signed/unsigned operands" >&2
    exit 1
fi

echo "verified: mixed signed/unsigned comparison lowers to unsigned LLVM compare"
