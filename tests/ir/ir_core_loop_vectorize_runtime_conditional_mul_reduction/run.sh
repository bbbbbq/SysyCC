#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
TEST_SOURCE="${SCRIPT_DIR}/ir_core_loop_vectorize_runtime_conditional_mul_reduction.sy"
IR_OUTPUT_FILE="${BUILD_DIR}/intermediate_results/ir_core_loop_vectorize_runtime_conditional_mul_reduction.ll"
COMPILER_BIN="${BUILD_DIR}/compiler"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${COMPILER_BIN}" "${TEST_SOURCE}" --dump-ir >/dev/null

if [[ ! -f "${IR_OUTPUT_FILE}" ]]; then
    echo "missing IR dump: ${IR_OUTPUT_FILE}" >&2
    exit 1
fi

grep -q "select i1" "${IR_OUTPUT_FILE}"
grep -q "phi i32" "${IR_OUTPUT_FILE}"
if grep -q "alloca i32" "${IR_OUTPUT_FILE}"; then
    echo "expected conditional reduction accumulator to leave the stack" >&2
    exit 1
fi

echo "verified: conditional runtime mul-reduction if-converts to branchless SSA"
