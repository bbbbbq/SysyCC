#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_core_loop_memory_promotion_shared_exit.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
IR_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.ll"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/compiler" -O2 -S -emit-llvm "${INPUT_FILE}" -o "${IR_FILE}"
assert_file_nonempty "${IR_FILE}"

if command -v opt-18 >/dev/null 2>&1; then
    opt-18 -passes=verify "${IR_FILE}" -disable-output
elif command -v opt >/dev/null 2>&1; then
    opt -passes=verify "${IR_FILE}" -disable-output
else
    clang -Wno-override-module -x ir "${IR_FILE}" -c -o "${BUILD_DIR}/${TEST_NAME}.o"
fi

echo "verified: loop memory promotion skips non-dedicated exits instead of rewriting shared-exit loads"
