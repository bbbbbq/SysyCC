#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/run_sum_two_ints.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
IR_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.ll"
PROGRAM_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.out"
RUNTIME_SOURCE="${PROJECT_ROOT}/tests/run/support/runtime_stub.c"
PROGRAM_INPUT="${SCRIPT_DIR}/run_sum_two_ints.in"
EXPECTED_OUTPUT="${SCRIPT_DIR}/run_sum_two_ints.out"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ir

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"
assert_file_nonempty "${IR_FILE}"
build_and_link_ir_executable "${IR_FILE}" "${RUNTIME_SOURCE}" "${PROGRAM_FILE}"
assert_program_output "${PROGRAM_FILE}" "${PROGRAM_INPUT}" "${EXPECTED_OUTPUT}"

grep -q '^declare i32 @getint()$' "${IR_FILE}"
grep -q '^declare void @putint(i32)$' "${IR_FILE}"
grep -q '^declare void @putch(i32)$' "${IR_FILE}"
grep -q 'call i32 @getint()' "${IR_FILE}"
grep -q 'call void @putint(i32 ' "${IR_FILE}"

echo "verified: runtime sum test compiles, links, and matches expected output"
