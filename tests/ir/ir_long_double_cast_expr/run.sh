#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_long_double_cast_expr.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
IR_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.ll"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ir

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"
assert_file_nonempty "${IR_FILE}"

grep -Eq 'sitofp i32 %t[0-9]+ to fp128' "${IR_FILE}"
grep -q 'fadd fp128 ' "${IR_FILE}"
grep -Eq 'fptrunc fp128 %t[0-9]+ to double' "${IR_FILE}"
grep -Eq 'fpext double %t[0-9]+ to fp128' "${IR_FILE}"
grep -Eq 'fptosi fp128 %t[0-9]+ to i32' "${IR_FILE}"

echo "verified: long double casts and arithmetic lower through fp128"
