#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
TEST_NAME="run_struct_forward_decl"
INPUT_FILE="${SCRIPT_DIR}/${TEST_NAME}.sy"
IR_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.ll"
OUTPUT_DIR="${SCRIPT_DIR}/build"
BIN_FILE="${OUTPUT_DIR}/${TEST_NAME}.out"
RUNTIME_SOURCE="${PROJECT_ROOT}/tests/run/support/runtime_stub.c"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
mkdir -p "${OUTPUT_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-ir

assert_file_nonempty "${IR_FILE}"

clang -Wno-override-module \
    "${IR_FILE}" \
    "${RUNTIME_SOURCE}" \
    -o "${BIN_FILE}"

"${BIN_FILE}"
ACTUAL_EXIT="$?"

if [[ "${ACTUAL_EXIT}" != "0" ]]; then
    echo "expected exit code 0, got ${ACTUAL_EXIT}" >&2
    exit 1
fi

echo "verified: runtime handles pointers to forward-declared structs"
