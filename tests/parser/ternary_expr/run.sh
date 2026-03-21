#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ternary_expr.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"

grep -q "^Question ? ${INPUT_FILE}:2:14-2:14$" \
    "${BUILD_DIR}/intermediate_results/${TEST_NAME}.tokens.txt"
grep -q "conditional_expr" \
    "${BUILD_DIR}/intermediate_results/${TEST_NAME}.parse.txt"

echo "verified: parser accepts ternary conditional expressions"
