#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_static_top_level_decls.sy"
IR_FILE="${BUILD_DIR}/intermediate_results/ir_static_top_level_decls.ll"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ir

assert_basic_frontend_outputs "${BUILD_DIR}" "ir_static_top_level_decls"
assert_file_nonempty "${IR_FILE}"
grep -q '^@g = internal global i32 1$' "${IR_FILE}"
if grep -q '@helper' "${IR_FILE}"; then
    grep -q '^define internal void @helper() {' "${IR_FILE}"
fi
grep -q '^define i32 @main() {' "${IR_FILE}"
grep -Eq '^  %t[0-9]+ = load i32, ptr @g$' "${IR_FILE}"

echo "verified: ir keeps static globals internal and allows dead static helpers to be removed"
