#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_long_long_min_literal.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
IR_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.ll"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ast --dump-ir

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"
assert_file_nonempty "${IR_FILE}"

grep -Eq '^  %t[0-9]+ = sub i64 %t[0-9]+, 9223372036854775807$' "${IR_FILE}"
grep -Eq '^  %t[0-9]+ = sub i64 %t[0-9]+, %t[0-9]+$' "${IR_FILE}"
grep -Eq '^  %t[0-9]+ = icmp slt i64 %t[0-9]+, %t[0-9]+$' "${IR_FILE}"

echo "verified: long long literals keep 64-bit type through IR lowering"
