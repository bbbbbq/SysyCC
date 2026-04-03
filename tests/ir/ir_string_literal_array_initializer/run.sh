#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_string_literal_array_initializer.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
IR_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.ll"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ir

assert_file_nonempty "${IR_FILE}"
grep -Fq '@text = global [4 x i8] c"abc\00"' "${IR_FILE}"
grep -Eq '%t[0-9]+ = getelementptr inbounds \[4 x i8\], ptr @text, i32 0, i32 1' "${IR_FILE}"
grep -Eq '%t[0-9]+ = load i8, ptr %t[0-9]+' "${IR_FILE}"
grep -Eq '%t[0-9]+ = sext i8 %t[0-9]+ to i32' "${IR_FILE}"

echo "verified: ir lowers string literal array initializers"
