#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ast_unknown_expr_preservation.sy"
AST_FILE="${BUILD_DIR}/intermediate_results/ast_unknown_expr_preservation.ast.txt"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
"${BUILD_DIR}/SysyCC" --dump-tokens --dump-parse --dump-ast "${INPUT_FILE}" >/tmp/sysycc_ast_unknown_expr_preservation.out 2>&1

grep -q "PrefixExpr ++" "${AST_FILE}"
grep -q "PostfixExpr ++" "${AST_FILE}"
grep -q "UnaryExpr &" "${AST_FILE}"
grep -q "UnaryExpr \\*" "${AST_FILE}"
grep -q "FloatLiteralExpr 1.5" "${AST_FILE}"
grep -q "CharLiteralExpr 'a'" "${AST_FILE}"
grep -q 'StringLiteralExpr "x"' "${AST_FILE}"

echo "verified: ast dump lowers unary, prefix/postfix, and literal expressions"
