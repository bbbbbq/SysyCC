#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_global_struct_field_address_initializer.sy"
IR_FILE="${BUILD_DIR}/intermediate_results/ir_global_struct_field_address_initializer.ll"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-ir

assert_file_nonempty "${IR_FILE}"
grep -q '^@value_ptr = global ptr getelementptr inbounds ({ i32, i32 }, ptr @pair, i32 0, i32 1)$' "${IR_FILE}"

echo "verified: ir lowers global struct field address initializers"
