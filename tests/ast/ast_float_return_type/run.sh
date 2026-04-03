#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ast_float_return_type.sy"
AST_FILE="${BUILD_DIR}/intermediate_results/ast_float_return_type.ast.txt"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" --stop-after=ast "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ast

grep -q "^TranslationUnit$" "${AST_FILE}"
grep -q "^  FunctionDecl value$" "${AST_FILE}"
grep -q "^    BuiltinType float$" "${AST_FILE}"
grep -q "^    BlockStmt$" "${AST_FILE}"
grep -q "^      ReturnStmt$" "${AST_FILE}"
grep -q "^        IntegerLiteralExpr 0$" "${AST_FILE}"

echo "verified: ast dump preserves builtin float return types"
