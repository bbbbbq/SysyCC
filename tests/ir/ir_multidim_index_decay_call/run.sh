#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_multidim_index_decay_call.sy"
IR_FILE="${BUILD_DIR}/intermediate_results/ir_multidim_index_decay_call.ll"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ir

assert_file_nonempty "${IR_FILE}"
grep -Eq 'getelementptr inbounds \[3 x i32\], ptr %t[0-9]+, i32 0' "${IR_FILE}"
grep -q 'call i32 @take_ptr(ptr' "${IR_FILE}"

echo "verified: ir lowers multidimensional index decay in call arguments"
