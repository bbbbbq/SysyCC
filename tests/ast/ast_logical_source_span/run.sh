#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ast_logical_source_span.sy"
AST_FILE="${BUILD_DIR}/intermediate_results/ast_logical_source_span.ast.txt"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ast

grep -q "^  FunctionDecl main$" "${AST_FILE}"
grep -q "^    SourceSpan virtual_ast.sy:40:1-42:1$" "${AST_FILE}"
grep -q "^      ReturnStmt$" "${AST_FILE}"
grep -q "^        SourceSpan virtual_ast.sy:41:5-41:13$" "${AST_FILE}"
grep -q "^        IntegerLiteralExpr 0$" "${AST_FILE}"
grep -q "^          SourceSpan virtual_ast.sy:41:12-41:12$" "${AST_FILE}"

echo "verified: ast dump inherits logical file and line mapping from preprocess output"
