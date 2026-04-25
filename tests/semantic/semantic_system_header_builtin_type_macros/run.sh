#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/semantic_system_header_builtin_type_macros.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
SYSTEM_DIR="${SCRIPT_DIR}/system_headers"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
"${BUILD_DIR}/SysyCC" --stop-after=semantic \
    -isystem "${SYSTEM_DIR}" \
    "${INPUT_FILE}" \
    --dump-tokens \
    --dump-parse
assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"

grep -q 'TYPE_NAME __PTRDIFF_TYPE__' "${BUILD_DIR}/intermediate_results/${TEST_NAME}.parse.txt"
grep -q 'TYPE_NAME __SIZE_TYPE__' "${BUILD_DIR}/intermediate_results/${TEST_NAME}.parse.txt"
grep -q 'TYPE_NAME __uint128_t' "${BUILD_DIR}/intermediate_results/${TEST_NAME}.parse.txt"

echo "verified: builtin type-macro spellings and __uint128_t survive system-header semantic analysis"
