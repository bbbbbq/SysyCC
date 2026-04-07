#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_struct_return_call_expr.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
IR_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.ll"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ast --dump-ir

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"
assert_file_nonempty "${IR_FILE}"

grep -Eq '^@g_pair = internal global \{ i32, i32 \} \{ i32 1, i32 7 \}$' "${IR_FILE}"
grep -Eq '^define i32 @main\(\) \{$' "${IR_FILE}"
grep -Eq '^  %t[0-9]+ = load \{ i32, i32 \}, ptr @g_pair$' "${IR_FILE}"
grep -Eq '^  %t[0-9]+ = getelementptr inbounds \{ i32, i32 \}, ptr @g_pair, i32 0, i32 1$' "${IR_FILE}"
grep -Eq '^  %t[0-9]+ = load i32, ptr %t[0-9]+$' "${IR_FILE}"
grep -Eq '^  ret i32 %t[0-9]+$' "${IR_FILE}"

echo "verified: aggregate-return helpers can optimize away while preserving the discarded struct-return semantics"
