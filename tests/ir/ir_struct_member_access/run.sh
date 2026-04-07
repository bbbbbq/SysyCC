#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_struct_member_access.sy"
IR_FILE="${BUILD_DIR}/intermediate_results/ir_struct_member_access.ll"
TEST_NAME="$(basename "${SCRIPT_DIR}")"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ir

assert_file_nonempty "${IR_FILE}"
if ! grep -Eq 'alloca \{ i32, i32 \}|^  ret i32 9$' "${IR_FILE}"; then
    echo "expected struct-member lowering either to keep the local struct storage or to fold to the final constant result" >&2
    exit 1
fi
if ! grep -Eq 'getelementptr inbounds \{ i32, i32 \}, ptr %pair\.addr, i32 0, i32 0|^  ret i32 9$' "${IR_FILE}"; then
    echo "expected struct-member lowering either to materialize the left-field GEP or to fold to the final constant result" >&2
    exit 1
fi
if ! grep -Eq 'getelementptr inbounds \{ i32, i32 \}, ptr %pair\.addr, i32 0, i32 1|^  ret i32 9$' "${IR_FILE}"; then
    echo "expected struct-member lowering either to materialize the right-field GEP or to fold to the final constant result" >&2
    exit 1
fi

echo "verified: ir lowers local struct storage and dot-member access"
