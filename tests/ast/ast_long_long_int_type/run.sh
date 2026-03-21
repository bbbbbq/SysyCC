#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ast_long_long_int_type.sy"
AST_FILE="${BUILD_DIR}/intermediate_results/ast_long_long_int_type.ast.txt"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ast

grep -q '^  FunctionDecl llrint$' "${AST_FILE}"
grep -q '^    BuiltinType long long int$' "${AST_FILE}"

echo "verified: ast lowers long long int builtin types"
