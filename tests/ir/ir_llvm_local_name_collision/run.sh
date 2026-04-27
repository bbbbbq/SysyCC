#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_llvm_local_name_collision.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
IR_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.ll"
OBJ_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.o"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" -S -emit-llvm -o "${IR_FILE}" --dump-ir

assert_file_nonempty "${IR_FILE}"
clang -x ir -c "${IR_FILE}" -o "${OBJ_FILE}"

echo "verified: LLVM lowering uniquifies parameter and temporary local names"
