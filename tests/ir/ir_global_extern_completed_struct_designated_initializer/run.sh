#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_global_extern_completed_struct_designated_initializer.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
IR_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.ll"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/compiler" -S -emit-llvm "${INPUT_FILE}" -o "${IR_FILE}"
assert_file_nonempty "${IR_FILE}"
grep -Fq 'c"GIT_TRACE_FSMONITOR' "${IR_FILE}"

echo "verified: extern incomplete struct redeclarations use completed type for global designated initializers"
