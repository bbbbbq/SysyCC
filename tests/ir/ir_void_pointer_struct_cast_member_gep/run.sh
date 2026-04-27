#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_void_pointer_struct_cast_member_gep.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
IR_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.ll"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" -S -emit-llvm -o "${IR_FILE}" --dump-ir

assert_file_nonempty "${IR_FILE}"
if grep -q "getelementptr inbounds void" "${IR_FILE}"; then
    echo "unexpected void-typed getelementptr after struct pointer cast" >&2
    exit 1
fi

echo "verified: struct member GEP keeps pointee type after void pointer cast"
