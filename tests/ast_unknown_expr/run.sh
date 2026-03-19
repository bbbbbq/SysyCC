#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ast_unknown_expr.sy"
AST_FILE="${BUILD_DIR}/intermediate_results/ast_unknown_expr.ast.txt"

cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}"
cmake --build "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ast

grep -q "^TranslationUnit$" "${AST_FILE}"
grep -q "^  FunctionDecl main$" "${AST_FILE}"
grep -q "^    BuiltinType int$" "${AST_FILE}"
grep -q "^      ReturnStmt$" "${AST_FILE}"
grep -q "^        IdentifierExpr value$" "${AST_FILE}"

echo "verified: ast dump lowers identifier expressions"
