#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ast_control_flow.sy"
AST_FILE="${BUILD_DIR}/intermediate_results/ast_control_flow.ast.txt"

cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}"
cmake --build "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ast

grep -q "^  FunctionDecl main$" "${AST_FILE}"
grep -q "^      WhileStmt$" "${AST_FILE}"
grep -q "^        Condition$" "${AST_FILE}"
grep -q "^          BinaryExpr <$" "${AST_FILE}"
grep -q "^        Body$" "${AST_FILE}"
grep -q "^          BlockStmt$" "${AST_FILE}"
grep -q "^            IfStmt$" "${AST_FILE}"
grep -q "^              Condition$" "${AST_FILE}"
grep -q "^                BinaryExpr ==$" "${AST_FILE}"
grep -q "^              Then$" "${AST_FILE}"
grep -q "^                  ExprStmt$" "${AST_FILE}"
grep -q "^                    AssignExpr$" "${AST_FILE}"
grep -q "^                  ContinueStmt$" "${AST_FILE}"
grep -q "^            ExprStmt$" "${AST_FILE}"
grep -q "^              AssignExpr$" "${AST_FILE}"

echo "verified: ast dump lowers while/if control flow and assignment statements"
