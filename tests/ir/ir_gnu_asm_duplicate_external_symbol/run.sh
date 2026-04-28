#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_gnu_asm_duplicate_external_symbol.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
IR_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.ll"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-ir

assert_file_nonempty "${IR_FILE}"
if [[ "$(grep -c '^declare i64 @lseek64(i32, i64, i32)$' "${IR_FILE}")" != "1" ]]; then
    echo "expected exactly one lseek64 declaration" >&2
    exit 1
fi
grep -Eq 'call i64 @lseek64\(i32 1, i64 2, i32 3\)' "${IR_FILE}"
grep -Eq 'call i64 @lseek64\(i32 4, i64 5, i32 6\)' "${IR_FILE}"

echo "verified: IR coalesces duplicate external function declarations by asm label"
