#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ast_top_level_decls.sy"
AST_FILE="${BUILD_DIR}/intermediate_results/ast_top_level_decls.ast.txt"

cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}"
cmake --build "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ast

grep -q "^  ConstDecl size$" "${AST_FILE}"
grep -q "^    BuiltinType int$" "${AST_FILE}"
grep -q "^      IntegerLiteralExpr 3$" "${AST_FILE}"
grep -q "^  VarDecl values$" "${AST_FILE}"
grep -q "^    Dimension$" "${AST_FILE}"
grep -q "^      InitListExpr$" "${AST_FILE}"
grep -q "^        IntegerLiteralExpr 1$" "${AST_FILE}"
grep -q "^        IntegerLiteralExpr 2$" "${AST_FILE}"
grep -q "^        IntegerLiteralExpr 3$" "${AST_FILE}"
grep -q "^        BinaryExpr +$" "${AST_FILE}"
grep -q "^          IndexExpr$" "${AST_FILE}"
grep -q "^            IdentifierExpr values$" "${AST_FILE}"
grep -q "^            IntegerLiteralExpr 0$" "${AST_FILE}"
grep -q "^          IdentifierExpr size$" "${AST_FILE}"

echo "verified: ast dump lowers top-level declarations and indexing"
