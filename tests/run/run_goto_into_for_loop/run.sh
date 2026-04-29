#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${SYSYCC_BUILD_DIR:-${PROJECT_ROOT}/build}"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
INPUT_FILE="${SCRIPT_DIR}/${TEST_NAME}.sy"
PROGRAM_INPUT="${SCRIPT_DIR}/${TEST_NAME}.in"
IR_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.ll"
OUTPUT_DIR="${SCRIPT_DIR}/build"
PROGRAM_BINARY="${OUTPUT_DIR}/${TEST_NAME}.out.bin"
COMPILER_BIN="${BUILD_DIR}/compiler"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

mkdir -p "$(dirname "${IR_FILE}")"
"${COMPILER_BIN}" -S -emit-llvm "${INPUT_FILE}" -o "${IR_FILE}" >/dev/null

assert_file_nonempty "${IR_FILE}"
mkdir -p "${OUTPUT_DIR}"
clang -Wno-override-module -x ir "${IR_FILE}" -o "${PROGRAM_BINARY}"
"${PROGRAM_BINARY}" <"${PROGRAM_INPUT}"

echo "verified: runtime executes goto into a labeled for-loop body"
