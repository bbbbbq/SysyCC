#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_global_multidim_flat_array_initializer.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
IR_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.ll"

export SYSYCC_TEST_DISABLE_HOST_TOOL_WRAPPERS=1

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ir

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"
assert_file_nonempty "${IR_FILE}"
grep -Eq '^@c = (internal )?global \[5 x \[3 x i32\]\] \[ \[3 x i32\] \[ i32 1, i32 2, i32 3 \], \[3 x i32\] \[ i32 4, i32 5, i32 6 \], \[3 x i32\] \[ i32 7, i32 8, i32 9 \], \[3 x i32\] \[ i32 10, i32 11, i32 12 \], \[3 x i32\] \[ i32 13, i32 14, i32 15 \] \]$' "${IR_FILE}"
grep -Eq '^@e = (internal )?global \[5 x \[3 x i32\]\] \[ \[3 x i32\] \[ i32 1, i32 2, i32 3 \], \[3 x i32\] \[ i32 4, i32 5, i32 6 \], \[3 x i32\] \[ i32 7, i32 8, i32 9 \], \[3 x i32\] \[ i32 10, i32 11, i32 12 \], \[3 x i32\] \[ i32 13, i32 14, i32 15 \] \]$' "${IR_FILE}"
grep -Eq '^@g = (internal )?global \[5 x \[3 x i32\]\]' "${IR_FILE}"
grep -Eq '\[3 x i32\] \[ i32 4, i32 0, i32 0 \], \[3 x i32\] \[ i32 7, i32 0, i32 0 \], \[3 x i32\] \[ i32 10, i32 11, i32 12 \]' "${IR_FILE}"
grep -Eq '^@i = (internal )?global \[2 x \[3 x \[4 x i32\]\]\]' "${IR_FILE}"
grep -Eq '\[4 x i32\] \[ i32 1, i32 2, i32 3, i32 4 \], \[4 x i32\] \[ i32 5, i32 0, i32 0, i32 0 \]' "${IR_FILE}"

echo "verified: global multidimensional flat initializers preserve nested aggregate shape"
