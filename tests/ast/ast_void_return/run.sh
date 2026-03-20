#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ast_void_return.sy"
AST_FILE="${BUILD_DIR}/intermediate_results/ast_void_return.ast.txt"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ast

grep -q "^TranslationUnit$" "${AST_FILE}"
grep -q "^  FunctionDecl main$" "${AST_FILE}"
grep -q "^    BuiltinType void$" "${AST_FILE}"
grep -q "^    BlockStmt$" "${AST_FILE}"
grep -q "^      ReturnStmt$" "${AST_FILE}"

if grep -q "^        IntegerLiteralExpr" "${AST_FILE}"; then
    echo "error: void return unexpectedly contains a value expression" >&2
    exit 1
fi

echo "verified: ast dump handles void returns"
