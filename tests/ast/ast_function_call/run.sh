#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ast_function_call.sy"
AST_FILE="${BUILD_DIR}/intermediate_results/ast_function_call.ast.txt"

cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}"
cmake --build "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ast

grep -q "^  FunctionDecl add$" "${AST_FILE}"
grep -q "^      ReturnStmt$" "${AST_FILE}"
grep -q "^        BinaryExpr +$" "${AST_FILE}"
grep -q "^          IdentifierExpr lhs$" "${AST_FILE}"
grep -q "^          IdentifierExpr rhs$" "${AST_FILE}"
grep -q "^  FunctionDecl main$" "${AST_FILE}"
grep -q "^        CallExpr$" "${AST_FILE}"
grep -q "^          Callee$" "${AST_FILE}"
grep -q "^            IdentifierExpr add$" "${AST_FILE}"
grep -q "^          Argument$" "${AST_FILE}"
grep -q "^            IntegerLiteralExpr 1$" "${AST_FILE}"
grep -q "^            IntegerLiteralExpr 2$" "${AST_FILE}"

echo "verified: ast dump lowers function calls and binary expressions"
