#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${SYSYCC_BUILD_DIR:-${PROJECT_ROOT}/build}"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
INPUT_FILE="${SCRIPT_DIR}/${TEST_NAME}.sy"
IR_FILE="${PROJECT_ROOT}/build/intermediate_results/${TEST_NAME}.ll"
OUTPUT_DIR="${SCRIPT_DIR}/build"
PROGRAM_BINARY="${OUTPUT_DIR}/${TEST_NAME}.out.bin"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/compiler" "${INPUT_FILE}" --dump-ir

assert_file_nonempty "${IR_FILE}"
mkdir -p "${OUTPUT_DIR}"
clang "${IR_FILE}" -fno-builtin -o "${PROGRAM_BINARY}"
"${PROGRAM_BINARY}"

echo "verified: local union initializers honor nested designators"
