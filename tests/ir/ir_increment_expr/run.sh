#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_increment_expr.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
IR_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.ll"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ir

assert_file_nonempty "${IR_FILE}"
grep -Eq '^  %i\.addr = alloca i32$' "${IR_FILE}"
grep -Eq '^  %j\.addr = alloca i32$' "${IR_FILE}"
grep -Eq 'load i32, ptr %i\.addr' "${IR_FILE}"
grep -Eq 'add i32 %t[0-9]+, 1' "${IR_FILE}"
grep -Eq 'store i32 %t[0-9]+, ptr %i\.addr' "${IR_FILE}"

echo "verified: ir lowers prefix and postfix increment expressions"
