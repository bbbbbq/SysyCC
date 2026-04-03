#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ast_pointer_target_cast_expr.sy"
AST_FILE="${BUILD_DIR}/intermediate_results/ast_pointer_target_cast_expr.ast.txt"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" --stop-after=ast "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ast

grep -q 'CastExpr' "${AST_FILE}"
grep -q 'PointerType' "${AST_FILE}"
grep -q 'BuiltinType int' "${AST_FILE}"
grep -q 'QualifiedType const' "${AST_FILE}"
grep -q 'BuiltinType char' "${AST_FILE}"

echo "verified: ast preserves pointer-target cast syntax"
