#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${SYSYCC_BUILD_DIR:-${PROJECT_ROOT}/build}"
INPUT_FILE="${SCRIPT_DIR}/run_packed_struct_global_array_initializer.sy"
IR_FILE="${BUILD_DIR}/intermediate_results/run_packed_struct_global_array_initializer.ll"
BINARY_FILE="${BUILD_DIR}/intermediate_results/run_packed_struct_global_array_initializer"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
"${BUILD_DIR}/SysyCC" -O -S -emit-llvm "${INPUT_FILE}" -o "${IR_FILE}"
grep -q "\\[2 x <{ ptr, i32, \\[4 x i8\\] }>\\]" "${IR_FILE}"
grep -q "<{ ptr, i32, \\[4 x i8\\] }> <{" "${IR_FILE}"
clang "${IR_FILE}" -Wno-override-module -o "${BINARY_FILE}"
"${BINARY_FILE}"

echo "verified: packed struct global array initializers emit LLVM packed constants"
