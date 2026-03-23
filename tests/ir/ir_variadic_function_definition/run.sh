#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_variadic_function_definition.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
IR_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.ll"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ir

assert_file_nonempty "${IR_FILE}"
grep -q '^define i32 @keep_first(i32 %x, ...) {$' "${IR_FILE}"
grep -Eq 'call i32 \(i32, \.\.\.\) @keep_first\(i32 7, double %t[0-9]+, i32 %t[0-9]+\)' "${IR_FILE}"

echo "verified: ir lowers variadic functions and applies default argument promotions"
