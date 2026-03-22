#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ast_comma_operator_expr.sy"
AST_FILE="${BUILD_DIR}/intermediate_results/ast_comma_operator_expr.ast.txt"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ast

assert_file_nonempty "${AST_FILE}"
grep -q '^        BinaryExpr ,$' "${AST_FILE}"
grep -q '^          IntegerLiteralExpr 1$' "${AST_FILE}"
grep -q '^          IntegerLiteralExpr 2$' "${AST_FILE}"

echo "verified: ast lowers comma operator expressions to BinaryExpr nodes"
