#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
CASE_BUILD_DIR="${SCRIPT_DIR}/build"
INPUT_FILE="${SCRIPT_DIR}/run_long_double_cast_expr.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
IR_FILE="${CASE_BUILD_DIR}/${TEST_NAME}.ll"
PROGRAM_FILE="${CASE_BUILD_DIR}/${TEST_NAME}.out"
RUNTIME_SOURCE="${PROJECT_ROOT}/tests/run/support/runtime_stub.c"
PROGRAM_INPUT="${SCRIPT_DIR}/run_long_double_cast_expr.in"
EXPECTED_OUTPUT="${SCRIPT_DIR}/run_long_double_cast_expr.out"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

mkdir -p "${CASE_BUILD_DIR}"
"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ir

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"
copy_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}" "${CASE_BUILD_DIR}"
copy_optional_frontend_output "${BUILD_DIR}" "${TEST_NAME}" "ll" "${CASE_BUILD_DIR}"
assert_file_nonempty "${CASE_BUILD_DIR}/${TEST_NAME}.preprocessed.sy"
assert_file_nonempty "${CASE_BUILD_DIR}/${TEST_NAME}.tokens.txt"
assert_file_nonempty "${CASE_BUILD_DIR}/${TEST_NAME}.parse.txt"
assert_file_nonempty "${IR_FILE}"
build_and_link_ir_executable "${IR_FILE}" "${RUNTIME_SOURCE}" "${PROGRAM_FILE}"
assert_program_output "${PROGRAM_FILE}" "${PROGRAM_INPUT}" "${EXPECTED_OUTPUT}"

grep -Eq 'sitofp i32 %t[0-9]+ to fp128' "${IR_FILE}"
grep -Eq 'fptrunc fp128 %t[0-9]+ to double' "${IR_FILE}"
grep -Eq 'fpext double %t[0-9]+ to fp128' "${IR_FILE}"
grep -Eq 'fptosi fp128 %t[0-9]+ to i32' "${IR_FILE}"

echo "verified: runtime long double casts produce the expected value"
