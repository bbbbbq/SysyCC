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
OUTPUT_DIR="${SCRIPT_DIR}/build"
PROGRAM_BINARY="${OUTPUT_DIR}/${TEST_NAME}.out.bin"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-ir

assert_file_nonempty "${IR_FILE}"
build_and_link_ir_executable "${IR_FILE}" \
    "${PROJECT_ROOT}/tests/run/support/runtime_stub.c" \
    "${PROGRAM_BINARY}"
assert_program_output "${PROGRAM_BINARY}" "${PROGRAM_INPUT}" "${EXPECTED_OUTPUT}"

if grep -q 'call void @putint(i32 4)' "${IR_FILE}"; then
    true
else
    grep -q '\[3 x i32\]' "${IR_FILE}"
    grep -q 'store i32 4, ptr %t0' "${IR_FILE}"
    if ! grep -Eq '^cond\.(false|true)[0-9]+:$|icmp eq i32 %t4, 4' "${IR_FILE}"; then
        echo "expected either preserved conditional blocks or the newer folded comparison form" >&2
        exit 1
    fi
fi

echo "verified: canonical conditional expressions stay constant through array dimensions enum values case labels and final comparisons"
