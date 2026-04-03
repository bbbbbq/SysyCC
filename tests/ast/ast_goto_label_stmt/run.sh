#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ast_goto_label_stmt.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" --stop-after=ast "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ast

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"
assert_file_nonempty "${BUILD_DIR}/intermediate_results/${TEST_NAME}.ast.txt"
grep -q 'GotoStmt done' "${BUILD_DIR}/intermediate_results/${TEST_NAME}.ast.txt"
grep -q 'LabelStmt done' "${BUILD_DIR}/intermediate_results/${TEST_NAME}.ast.txt"

echo "verified: ast preserves goto statements and labels"
