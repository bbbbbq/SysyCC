#!/usr/bin/env bash

set -euo pipefail

CASE_DIR="${1:?case directory is required}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
TEST_NAME="$(basename "${CASE_DIR}")"
INPUT_FILE="${CASE_DIR}/${TEST_NAME}.sy"
PROGRAM_INPUT="${CASE_DIR}/${TEST_NAME}.in"
EXPECTED_OUTPUT="${CASE_DIR}/${TEST_NAME}.out"
IR_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.ll"
OUTPUT_DIR="${CASE_DIR}/build"
PROGRAM_BINARY="${OUTPUT_DIR}/${TEST_NAME}.out.bin"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

if [[ -n "${SYSYCC_RUN_COMPILER_FLAGS:-}" ]]; then
    COMPILER_FLAGS=()
    read -r -a COMPILER_FLAGS <<<"${SYSYCC_RUN_COMPILER_FLAGS}"
    "${BUILD_DIR}/SysyCC" "${COMPILER_FLAGS[@]}" "${INPUT_FILE}" --dump-ir
else
    "${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-ir
fi

assert_file_nonempty "${IR_FILE}"
build_and_link_ir_executable "${IR_FILE}" \
    "${PROJECT_ROOT}/tests/run/support/runtime_stub.c" \
    "${PROGRAM_BINARY}"
assert_program_output "${PROGRAM_BINARY}" "${PROGRAM_INPUT}" "${EXPECTED_OUTPUT}"

echo "verified: ${TEST_NAME} executes correctly"
