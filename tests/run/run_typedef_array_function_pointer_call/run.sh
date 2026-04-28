#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${SYSYCC_BUILD_DIR:-${PROJECT_ROOT}/build}"
INPUT_FILE="${SCRIPT_DIR}/run_typedef_array_function_pointer_call.sy"
IR_FILE="${BUILD_DIR}/intermediate_results/run_typedef_array_function_pointer_call.ll"
BINARY_FILE="${BUILD_DIR}/intermediate_results/run_typedef_array_function_pointer_call"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
"${BUILD_DIR}/SysyCC" -O -S -emit-llvm "${INPUT_FILE}" -o "${IR_FILE}"
clang "${IR_FILE}" -Wno-override-module -o "${BINARY_FILE}"
"${BINARY_FILE}"

echo "verified: typedef array function-pointer parameters use pointer-adjusted calls"
