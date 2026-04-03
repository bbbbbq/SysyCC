#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ast_pointer_nullability_parameter_prototype.sy"
AST_FILE="${BUILD_DIR}/intermediate_results/ast_pointer_nullability_parameter_prototype.ast.txt"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" --stop-after=ast "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ast

grep -q '^  FunctionDecl nullable_callback$' "${AST_FILE}"
grep -q '^  FunctionDecl nonnull_callback$' "${AST_FILE}"
grep -q '^  FunctionDecl unspecified_callback$' "${AST_FILE}"
grep -q '^    ParamDecl <unnamed>$' "${AST_FILE}"
grep -q '^      PointerType$' "${AST_FILE}"
grep -q '^        PointerNullability nullable$' "${AST_FILE}"
grep -q '^        PointerNullability nonnull$' "${AST_FILE}"
grep -q '^        PointerNullability null_unspecified$' "${AST_FILE}"
grep -q '^        FunctionType$' "${AST_FILE}"
grep -q '^          BuiltinType int$' "${AST_FILE}"

echo "verified: ast preserves pointer-side nullability spellings on anonymous function-pointer parameters"
