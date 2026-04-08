#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
TEST_SOURCE="${SCRIPT_DIR}/ir_core_loop_vectorize.sy"
IR_OUTPUT_FILE="${BUILD_DIR}/intermediate_results/ir_core_loop_vectorize.ll"
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
grep -q "load <4 x i32>" "${IR_OUTPUT_FILE}"
grep -q "store <4 x i32>" "${IR_OUTPUT_FILE}"
grep -q "insertelement <4 x i32>" "${IR_OUTPUT_FILE}"
grep -q "shufflevector <4 x i32>" "${IR_OUTPUT_FILE}"

echo "verified: LoopVectorize emits vector.body blocks and vector broadcast/store lanes on a mm-style loop"
