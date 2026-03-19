#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ast_unary_literals.sy"
AST_FILE="${BUILD_DIR}/intermediate_results/ast_unary_literals.ast.txt"
TEST_NAME="$(basename "${SCRIPT_DIR}")"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ast

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"
assert_file_nonempty "${AST_FILE}"

grep -q '^        UnaryExpr -$' "${AST_FILE}"
grep -q '^        UnaryExpr !$' "${AST_FILE}"
grep -q '^        UnaryExpr ~$' "${AST_FILE}"
grep -q '^        PrefixExpr --$' "${AST_FILE}"
grep -q '^        PostfixExpr --$' "${AST_FILE}"
grep -q '^        UnaryExpr &$' "${AST_FILE}"
grep -q '^        UnaryExpr \*$' "${AST_FILE}"
grep -q '^        FloatLiteralExpr 2.5$' "${AST_FILE}"
grep -q "^        CharLiteralExpr 'z'$" "${AST_FILE}"
grep -q '^        StringLiteralExpr "hello"$' "${AST_FILE}"

echo "verified: ast dump lowers unary, prefix/postfix, and literal expression variants"
