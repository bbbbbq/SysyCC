#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${SYSYCC_BUILD_DIR:-${PROJECT_ROOT}/build}"
INPUT_FILE="${SCRIPT_DIR}/run_builtin_ctz_smoke.sy"
IR_FILE="${BUILD_DIR}/intermediate_results/run_builtin_ctz_smoke.ll"
BINARY_FILE="${BUILD_DIR}/intermediate_results/run_builtin_ctz_smoke"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
"${BUILD_DIR}/SysyCC" -O -S -emit-llvm "${INPUT_FILE}" -o "${IR_FILE}"
grep -q "llvm.cttz.i64" "${IR_FILE}"
clang "${IR_FILE}" -o "${BINARY_FILE}"
"${BINARY_FILE}"

echo "verified: builtin ctzll lowers to executable LLVM cttz intrinsic"
