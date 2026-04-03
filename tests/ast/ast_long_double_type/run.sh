#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ast_long_double_type.sy"
AST_FILE="${BUILD_DIR}/intermediate_results/ast_long_double_type.ast.txt"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" --stop-after=ast "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ast

grep -q '^  FunctionDecl __fpclassifyl$' "${AST_FILE}"
grep -q '^    BuiltinType long double$' "${AST_FILE}"
grep -q '^    ParamDecl <unnamed>$' "${AST_FILE}"
grep -q '^      BuiltinType long double$' "${AST_FILE}"

echo "verified: ast lowers long double function prototypes"
