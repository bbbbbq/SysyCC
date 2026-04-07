#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
CASE_BUILD_DIR="${SCRIPT_DIR}/build"
INPUT_FILE="${SCRIPT_DIR}/run_while_constant_true_return.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
IR_FILE="${CASE_BUILD_DIR}/${TEST_NAME}.ll"
PROGRAM_FILE="${CASE_BUILD_DIR}/${TEST_NAME}.out"
RUNTIME_SOURCE="${PROJECT_ROOT}/tests/run/support/runtime_stub.c"
EXPECTED_OUTPUT="${SCRIPT_DIR}/run_while_constant_true_return.out"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

mkdir -p "${CASE_BUILD_DIR}"
"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-ir

copy_optional_frontend_output "${BUILD_DIR}" "${TEST_NAME}" "ll" "${CASE_BUILD_DIR}"
assert_file_nonempty "${IR_FILE}"
build_and_link_ir_executable "${IR_FILE}" "${RUNTIME_SOURCE}" "${PROGRAM_FILE}"
assert_program_output "${PROGRAM_FILE}" /dev/null "${EXPECTED_OUTPUT}"

if grep -Eq '^while\\.end0:$' "${IR_FILE}"; then
    echo "unexpected while.end block for constant-true no-break loop" >&2
    exit 1
fi

grep -Eq '^  ret i32 %t[0-9]+$|^  ret i32 7$' "${IR_FILE}"

echo "verified: constant-true while loops with return paths lower without empty continuation blocks"
