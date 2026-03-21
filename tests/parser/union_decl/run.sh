#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/union_decl.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"
grep -q '^KwUnion union ' \
    "${BUILD_DIR}/intermediate_results/${TEST_NAME}.tokens.txt"
grep -q '^KwUnsigned unsigned ' \
    "${BUILD_DIR}/intermediate_results/${TEST_NAME}.tokens.txt"
grep -q 'union_specifier' \
    "${BUILD_DIR}/intermediate_results/${TEST_NAME}.parse.txt"

echo "verified: parser accepts union declarations with unsigned field types"
