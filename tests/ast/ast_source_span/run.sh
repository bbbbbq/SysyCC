#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ast_source_span.sy"
AST_FILE="${BUILD_DIR}/intermediate_results/ast_source_span.ast.txt"

cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}"
cmake --build "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ast

grep -q "^  FunctionDecl main$" "${AST_FILE}"
grep -q "^    SourceSpan 1:1-3:1$" "${AST_FILE}"
grep -q "^      ReturnStmt$" "${AST_FILE}"
grep -q "^        SourceSpan 2:5-2:13$" "${AST_FILE}"
grep -q "^        IntegerLiteralExpr 0$" "${AST_FILE}"
grep -q "^          SourceSpan 2:12-2:12$" "${AST_FILE}"

echo "verified: ast dump preserves source spans from parse tree nodes"
