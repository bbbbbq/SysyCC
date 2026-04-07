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
if ! grep -Eq '^  %j\.addr = alloca i32$|^  ret i32 4$' "${IR_FILE}"; then
    echo "expected increment lowering either to keep the temporary stack slot or to fold to the final constant result" >&2
    exit 1
fi
if ! grep -Eq '^  store i32 4, ptr %j\.addr$|^  ret i32 4$' "${IR_FILE}"; then
    echo "expected increment lowering either to keep the final store or to fold to the final constant result" >&2
    exit 1
fi
grep -Eq '^  ret i32 4$' "${IR_FILE}"

echo "verified: ir lowers prefix and postfix increment expressions"
