#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_pointer_cast_struct_member_gep.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
IR_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.ll"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" -S -emit-llvm -O2 -o "${IR_FILE}" \
    --dump-core-ir --dump-ir >/dev/null

assert_file_nonempty "${IR_FILE}"
if grep -q "getelementptr inbounds \\[1024 x i8\\].*, i32 0, i32 0, i32 1" "${IR_FILE}"; then
    echo "pointer reinterpret member GEP was incorrectly flattened through the char buffer" >&2
    exit 1
fi

echo "verified: pointer reinterpret member GEP preserves the aggregate view"
