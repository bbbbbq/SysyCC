#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_local_pointer_array_initializer.sy"
IR_FILE="${BUILD_DIR}/intermediate_results/ir_local_pointer_array_initializer.ll"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ir

assert_file_nonempty "${IR_FILE}"
grep -q '^@global_value = global i32 7$' "${IR_FILE}"
if ! grep -Eq 'alloca \[2 x ptr\]|load i32, ptr @global_value' "${IR_FILE}"; then
    echo "expected local pointer-array initialization either to keep the local array object or to fold through the referenced global value" >&2
    exit 1
fi
if ! grep -Eq 'store ptr @global_value|load i32, ptr @global_value' "${IR_FILE}"; then
    echo "expected local pointer-array initialization either to materialize the global pointer store or to fold directly to a global load" >&2
    exit 1
fi

echo "verified: ir lowers local pointer array initializers"
