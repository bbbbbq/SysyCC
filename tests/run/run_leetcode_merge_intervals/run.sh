#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
CASE_BUILD_DIR="${SCRIPT_DIR}/build"
INPUT_FILE="${SCRIPT_DIR}/run_leetcode_merge_intervals.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
IR_FILE="${CASE_BUILD_DIR}/${TEST_NAME}.ll"
CORE_IR_FILE="${CASE_BUILD_DIR}/${TEST_NAME}.core-ir.txt"
PROGRAM_FILE="${CASE_BUILD_DIR}/${TEST_NAME}.out"
RUNTIME_SOURCE="${PROJECT_ROOT}/tests/run/support/runtime_stub.c"
PROGRAM_INPUT="${SCRIPT_DIR}/run_leetcode_merge_intervals.in"
EXPECTED_OUTPUT="${SCRIPT_DIR}/run_leetcode_merge_intervals.out"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

mkdir -p "${CASE_BUILD_DIR}"
"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-core-ir --dump-ir

copy_optional_frontend_output "${BUILD_DIR}" "${TEST_NAME}" "core-ir.txt" "${CASE_BUILD_DIR}"
copy_optional_frontend_output "${BUILD_DIR}" "${TEST_NAME}" "ll" "${CASE_BUILD_DIR}"
assert_file_nonempty "${CORE_IR_FILE}"
assert_file_nonempty "${IR_FILE}"

build_and_link_ir_executable "${IR_FILE}" "${RUNTIME_SOURCE}" "${PROGRAM_FILE}"
assert_program_output "${PROGRAM_FILE}" "${PROGRAM_INPUT}" "${EXPECTED_OUTPUT}"

grep -q 'func @sort_intervals' "${CORE_IR_FILE}"
grep -q 'call void @putint' "${IR_FILE}"

echo "verified: LCSSA no longer produces invalid shared-exit phis in merge-intervals style loops"
