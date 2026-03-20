#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ast_multiple_functions.sy"
AST_FILE="${BUILD_DIR}/intermediate_results/ast_multiple_functions.ast.txt"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ast

grep -q "^TranslationUnit$" "${AST_FILE}"
grep -q "^  FunctionDecl helper$" "${AST_FILE}"
grep -q "^  FunctionDecl main$" "${AST_FILE}"
grep -q "^        IntegerLiteralExpr 1$" "${AST_FILE}"
grep -q "^        IntegerLiteralExpr 0$" "${AST_FILE}"

echo "verified: ast dump collects multiple top-level functions"
