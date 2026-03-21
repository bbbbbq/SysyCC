#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_pointer_sub_offset.sy"
IR_FILE="${BUILD_DIR}/intermediate_results/ir_pointer_sub_offset.ll"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ir

assert_file_nonempty "${IR_FILE}"
grep -q 'sub i32 0,' "${IR_FILE}"
grep -q 'getelementptr inbounds i32, ptr %' "${IR_FILE}"

echo "verified: ir lowers pointer subtraction by integer offsets"
