#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_nullable_function_pointer_parameter_prototype.sy"
IR_FILE="${BUILD_DIR}/intermediate_results/ir_nullable_function_pointer_parameter_prototype.ll"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ir

assert_file_nonempty "${IR_FILE}"
grep -q '^define i32 @funopen_like(ptr %cookie, ptr %read_fn, ptr %write_fn, ptr %seek_fn)' "${IR_FILE}"

echo "verified: ir strips nullability-marked function pointer parameters to ptr"
