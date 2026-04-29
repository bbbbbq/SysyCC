#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
INPUT_FILE="${SCRIPT_DIR}/${TEST_NAME}.sy"
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
"${PROGRAM_BINARY}"

echo "verified: GNU inline asm operand barriers parse and lower conservatively"
