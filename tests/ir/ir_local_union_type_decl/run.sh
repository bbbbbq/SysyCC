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
grep -q 'alloca i32' "${IR_FILE}"
grep -q 'bitcast ptr %bits.addr to ptr' "${IR_FILE}"
grep -q 'lshr i32' "${IR_FILE}"

echo "verified: ir lowers local anonymous union declarations and shift expressions"
