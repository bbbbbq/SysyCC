#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_floating_comparison_and_condition.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
IR_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.ll"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ir

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"
assert_file_nonempty "${IR_FILE}"

grep -Eq 'fcmp olt float ' "${IR_FILE}"
grep -Eq 'fcmp une half ' "${IR_FILE}"
grep -Eq 'fcmp oeq double ' "${IR_FILE}"
grep -Eq 'fcmp ogt fp128 ' "${IR_FILE}"
grep -Eq 'fcmp une float ' "${IR_FILE}"
grep -Eq 'fcmp une half ' "${IR_FILE}"
grep -Eq 'fcmp une double ' "${IR_FILE}"
grep -Eq 'fcmp une fp128 ' "${IR_FILE}"
grep -Eq 'br i1 %t[0-9]+, label %' "${IR_FILE}"

echo "verified: floating comparison and conditional lowering lower through fcmp/br i1 across supported floating types"
