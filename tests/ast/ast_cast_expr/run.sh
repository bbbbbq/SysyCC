#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ast_cast_expr.sy"
AST_FILE="${BUILD_DIR}/intermediate_results/ast_cast_expr.ast.txt"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" --stop-after=ast "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ast

grep -q "^        CastExpr$" "${AST_FILE}"
grep -q "^          BuiltinType int$" "${AST_FILE}"
grep -q "^          IdentifierExpr value$" "${AST_FILE}"

echo "verified: ast dump lowers C-style casts to CastExpr"
