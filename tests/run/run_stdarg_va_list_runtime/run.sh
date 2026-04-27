#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${SYSYCC_BUILD_DIR:-${PROJECT_ROOT}/build}"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
INPUT_FILE="${SCRIPT_DIR}/${TEST_NAME}.sy"
PROGRAM_INPUT="${SCRIPT_DIR}/${TEST_NAME}.in"
EXPECTED_OUTPUT="/dev/null"
IR_FILE="${PROJECT_ROOT}/build/intermediate_results/${TEST_NAME}.ll"
OUTPUT_DIR="${SCRIPT_DIR}/build"
PROGRAM_BINARY="${OUTPUT_DIR}/${TEST_NAME}.out.bin"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-ir

assert_file_nonempty "${IR_FILE}"
if ! grep -q "llvm.va_start" "${IR_FILE}"; then
    echo "error: expected va_start to lower to llvm.va_start" >&2
    exit 1
fi
if ! grep -q "llvm.va_end" "${IR_FILE}"; then
    echo "error: expected va_end to lower to llvm.va_end" >&2
    exit 1
fi

mkdir -p "${OUTPUT_DIR}"
clang "${IR_FILE}" -Wno-override-module -fno-builtin -o "${PROGRAM_BINARY}"
assert_program_output "${PROGRAM_BINARY}" "${PROGRAM_INPUT}" "${EXPECTED_OUTPUT}"

echo "verified: stdarg va_list runtime handles int, unsigned long, and double"
