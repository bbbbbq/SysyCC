#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
TEST_SOURCE="${SCRIPT_DIR}/ir_core_loop_vectorize_runtime_mul_reduction_float.sy"
IR_OUTPUT_FILE="${BUILD_DIR}/intermediate_results/ir_core_loop_vectorize_runtime_mul_reduction_float.ll"
COMPILER_BIN="${BUILD_DIR}/compiler"
RUNTIME_HEADER="${PROJECT_ROOT}/tests/compiler2025/sylib.h"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${COMPILER_BIN}" -include "${RUNTIME_HEADER}" "${TEST_SOURCE}" --dump-ir >/dev/null

if [[ ! -f "${IR_OUTPUT_FILE}" ]]; then
    echo "missing IR dump: ${IR_OUTPUT_FILE}" >&2
    exit 1
fi

grep -q "vector.body:" "${IR_OUTPUT_FILE}"
grep -q "load <4 x float>" "${IR_OUTPUT_FILE}"
grep -q "fmul <4 x float>" "${IR_OUTPUT_FILE}"
grep -q "extractelement <4 x float>" "${IR_OUTPUT_FILE}"
grep -q "fadd float" "${IR_OUTPUT_FILE}"
if grep -q "pair.guard:" "${IR_OUTPUT_FILE}"; then
    echo "unexpected pair-interleave fallback in float runtime mul reduction" >&2
    exit 1
fi

echo "verified: LoopVectorize emits float runtime mul-reduction lanes"
