#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_extern_const_object_decl.sy"
IR_FILE="${BUILD_DIR}/intermediate_results/ir_extern_const_object_decl.ll"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ir

assert_file_nonempty "${IR_FILE}"
grep -q '^@signgam = external global i32$' "${IR_FILE}"
grep -q '^@sys_errptr = external global ptr$' "${IR_FILE}"
grep -q 'load i32, ptr @signgam' "${IR_FILE}"

echo "verified: ir lowers extern const object declarations"
