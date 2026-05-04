#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
TEST_SOURCE="${SCRIPT_DIR}/ir_core_if_chain_array_reduction_compaction.sy"
IR_OUTPUT_FILE="${BUILD_DIR}/intermediate_results/ir_core_if_chain_array_reduction_compaction.ll"
COMPILER_BIN="${BUILD_DIR}/compiler"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${COMPILER_BIN}" -O1 "${TEST_SOURCE}" --dump-ir >/dev/null

assert_file_nonempty "${IR_OUTPUT_FILE}"

if grep -q "alloca \\[8 x i32\\]" "${IR_OUTPUT_FILE}"; then
    echo "unexpected local array alloca remained after compaction" >&2
    exit 1
fi

grep -q "select i1" "${IR_OUTPUT_FILE}"
grep -q "mul i64" "${IR_OUTPUT_FILE}"
grep -q "srem i64" "${IR_OUTPUT_FILE}"

echo "verified: monotone if-chain array reduction compacts to straight-line SSA"
