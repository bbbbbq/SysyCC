#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
TEST_SOURCE="${SCRIPT_DIR}/ir_core_loop_pair_interleave_runtime_reduction.sy"
IR_OUTPUT_FILE="${BUILD_DIR}/intermediate_results/ir_core_loop_pair_interleave_runtime_reduction.ll"
COMPILER_BIN="${BUILD_DIR}/compiler"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${COMPILER_BIN}" "${TEST_SOURCE}" --dump-ir >/dev/null

assert_file_nonempty "${IR_OUTPUT_FILE}"

grep -q "pair.guard:" "${IR_OUTPUT_FILE}"
grep -q "pair.body.preheader:" "${IR_OUTPUT_FILE}"
grep -q "pair.body:" "${IR_OUTPUT_FILE}"
grep -q "pair.loopexit:" "${IR_OUTPUT_FILE}"

echo "verified: loop vectorize emits pair-interleave runtime reduction blocks"
