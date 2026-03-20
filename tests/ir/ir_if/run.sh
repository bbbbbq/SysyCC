#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_if.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
IR_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.ll"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ir

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"
assert_file_nonempty "${IR_FILE}"

grep -q 'icmp slt i32 1, 2' "${IR_FILE}"
grep -q 'zext i1 ' "${IR_FILE}"
grep -q 'br i1 ' "${IR_FILE}"
grep -q '^if.then0:$' "${IR_FILE}"
grep -q '^if.else2:$' "${IR_FILE}"
grep -q '^if.end1:$' "${IR_FILE}"
grep -q '^  store i32 3, ptr %x.addr$' "${IR_FILE}"
grep -q '^  store i32 4, ptr %x.addr$' "${IR_FILE}"

echo "verified: if/else lowers to compare and branch blocks"
