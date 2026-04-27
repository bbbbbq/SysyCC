#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/system_header_typedef_prescan_function_pointer.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" -fsyntax-only --dump-tokens --dump-parse --dump-ast

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"

grep -q 'TYPE_NAME CallInfo' "${BUILD_DIR}/intermediate_results/${TEST_NAME}.parse.txt"
grep -q 'IDENTIFIER L' "${BUILD_DIR}/intermediate_results/${TEST_NAME}.parse.txt"

echo "verified: typedef pre-scan keeps function pointer parameter names as identifiers"
