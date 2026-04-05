#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_global_array_index.sy"
IR_FILE="${BUILD_DIR}/intermediate_results/ir_global_array_index.ll"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ir

assert_file_nonempty "${IR_FILE}"
grep -q '^@values = global \[4 x i32\] zeroinitializer$' "${IR_FILE}"
grep -q 'getelementptr inbounds \[4 x i32\], ptr @values, i32 0, i32 2' "${IR_FILE}"

echo "verified: ir lowers global array definition and indexed access"
