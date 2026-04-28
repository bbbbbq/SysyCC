#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_core_inliner_loop_phi_return_guard.sy"
IR_FILE="${BUILD_DIR}/intermediate_results/ir_core_inliner_loop_phi_return_guard.ll"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
"${BUILD_DIR}/SysyCC" -O -S -emit-llvm "${INPUT_FILE}" -o "${IR_FILE}" --dump-ir
clang -Wno-override-module -x ir -c "${IR_FILE}" -o /dev/null

echo "verified: inliner does not produce broken SSA for loop phi return callees"
