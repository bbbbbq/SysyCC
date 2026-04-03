#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_global_union_member_address_initializer.sy"
IR_FILE="${BUILD_DIR}/intermediate_results/ir_global_union_member_address_initializer.ll"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ir

assert_file_nonempty "${IR_FILE}"
grep -F '@g_ptr = internal global ptr getelementptr inbounds ({ i32 }, ptr @g_payload, i32 0, i32 0)' "${IR_FILE}"

echo "verified: ir lowers union member address initializers without invalid gep indices"
