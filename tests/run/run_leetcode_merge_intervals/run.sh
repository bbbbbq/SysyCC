#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
INPUT_FILE="${SCRIPT_DIR}/${TEST_NAME}.sy"
PROGRAM_INPUT="${SCRIPT_DIR}/${TEST_NAME}.in"
EXPECTED_OUTPUT="${SCRIPT_DIR}/${TEST_NAME}.out"
IR_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.ll"
CORE_IR_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.core-ir.txt"
OUTPUT_DIR="${SCRIPT_DIR}/build"
PROGRAM_BINARY="${OUTPUT_DIR}/${TEST_NAME}.out.bin"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-core-ir --dump-ir

assert_file_nonempty "${IR_FILE}"
assert_file_nonempty "${CORE_IR_FILE}"
build_and_link_ir_executable "${IR_FILE}" \
    "${PROJECT_ROOT}/tests/run/support/runtime_stub.c" \
    "${PROGRAM_BINARY}"
assert_program_output "${PROGRAM_BINARY}" "${PROGRAM_INPUT}" "${EXPECTED_OUTPUT}"

grep -q 'func @sort_intervals' "${CORE_IR_FILE}"
grep -q 'call void @putint' "${IR_FILE}"

echo "verified: LCSSA no longer produces invalid shared-exit phis in merge-intervals style loops"
