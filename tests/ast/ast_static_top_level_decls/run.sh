#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ast_static_top_level_decls.sy"
AST_FILE="${BUILD_DIR}/intermediate_results/ast_static_top_level_decls.ast.txt"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" --stop-after=ast "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ast

grep -q '^  VarDecl g$' "${AST_FILE}"
grep -q '^    Static$' "${AST_FILE}"
grep -q '^  FunctionDecl helper$' "${AST_FILE}"
grep -q '^    Static$' "${AST_FILE}"

echo "verified: ast preserves static top-level declarations"
