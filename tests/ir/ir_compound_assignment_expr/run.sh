#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_compound_assignment_expr.sy"
IR_FILE="${BUILD_DIR}/intermediate_results/ir_compound_assignment_expr.ll"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ir

assert_file_nonempty "${IR_FILE}"
grep -q 'ashr i32' "${IR_FILE}"
grep -q 'shl i32' "${IR_FILE}"
grep -q 'or i32' "${IR_FILE}"
grep -q 'and i32' "${IR_FILE}"
grep -q 'xor i32' "${IR_FILE}"
grep -q 'add i32' "${IR_FILE}"
grep -q 'sub i32' "${IR_FILE}"
grep -q 'sdiv i32' "${IR_FILE}"
grep -q 'srem i32' "${IR_FILE}"

echo "verified: ir lowers compound assignment expressions"
