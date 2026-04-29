#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${SYSYCC_BUILD_DIR:-${PROJECT_ROOT}/build}"
TEST_SOURCE="${SCRIPT_DIR}/ir_core_loop_vectorize_runtime_add_guard.c"
IR_OUTPUT_FILE="${BUILD_DIR}/intermediate_results/ir_core_loop_vectorize_runtime_add_guard.ll"
COMPILER_BIN="${BUILD_DIR}/compiler"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${COMPILER_BIN}" -S -emit-llvm -O2 "${TEST_SOURCE}" \
    -o "${IR_OUTPUT_FILE}" >/dev/null

assert_file_nonempty "${IR_OUTPUT_FILE}"

if grep -q "vector.guard:" "${IR_OUTPUT_FILE}"; then
    echo "unsafe runtime add-reduction loop was vectorized" >&2
    exit 1
fi
if command -v opt-18 >/dev/null 2>&1; then
    opt-18 -passes=verify "${IR_OUTPUT_FILE}" -disable-output
fi

echo "verified: LoopVectorize skips unsafe runtime add-reduction guards"
