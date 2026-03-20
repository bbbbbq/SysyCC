#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ast_unknown_guard.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
AST_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.ast.txt"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ast

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"
assert_file_nonempty "${AST_FILE}"

grep -q '^  FunctionDecl main$' "${AST_FILE}"
grep -q '^        CallExpr$' "${AST_FILE}"
if grep -q 'UnknownExpr' "${AST_FILE}"; then
    echo "zero-argument calls should no longer lower to UnknownExpr" >&2
    exit 1
fi

echo "verified: zero-argument calls lower to CallExpr without UnknownExpr placeholders"
