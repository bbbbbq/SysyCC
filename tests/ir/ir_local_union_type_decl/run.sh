#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_local_union_type_decl.sy"
IR_FILE="${BUILD_DIR}/intermediate_results/ir_local_union_type_decl.ll"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ir

assert_file_nonempty "${IR_FILE}"
if ! grep -Eq 'alloca \{ i32 \}|^  ret i32 1$' "${IR_FILE}"; then
    echo "expected local union lowering either to keep the backing storage or to fold to the final constant result" >&2
    exit 1
fi
if ! grep -Eq '^  %t[0-9]+ = getelementptr inbounds \{ i32 \}, ptr %bits\.addr, i32 0, i32 0$|^  ret i32 1$' "${IR_FILE}"; then
    echo "expected local union lowering either to materialize the member GEP or to fold to the final constant result" >&2
    exit 1
fi
grep -Eq '^  ret i32 1$' "${IR_FILE}"

echo "verified: ir lowers local anonymous union declarations and shift expressions"
