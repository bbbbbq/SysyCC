#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${SYSYCC_BUILD_DIR:-${PROJECT_ROOT}/build}"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
INPUT_FILE="${SCRIPT_DIR}/${TEST_NAME}.sy"
PROGRAM_INPUT="${SCRIPT_DIR}/${TEST_NAME}.in"
EXPECTED_OUTPUT="${SCRIPT_DIR}/${TEST_NAME}.out"
IR_FILE="${PROJECT_ROOT}/build/intermediate_results/${TEST_NAME}.ll"
OUTPUT_DIR="${SCRIPT_DIR}/build"
PROGRAM_BINARY="${OUTPUT_DIR}/${TEST_NAME}.out.bin"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-ir

assert_file_nonempty "${IR_FILE}"
if ! grep -q "alloca i8" "${IR_FILE}"; then
    echo "error: __builtin_alloca did not lower to an LLVM alloca" >&2
    exit 1
fi
clang "${IR_FILE}" -fno-builtin -o "${PROGRAM_BINARY}"
assert_program_output "${PROGRAM_BINARY}" "${PROGRAM_INPUT}" "${EXPECTED_OUTPUT}"

echo "verified: __builtin_alloca lowers to stack storage and executes correctly"
