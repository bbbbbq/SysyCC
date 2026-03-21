#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ast_union_decl.sy"
AST_FILE="${BUILD_DIR}/intermediate_results/ast_union_decl.ast.txt"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ast

grep -q '^          UnionType <anonymous>$' "${AST_FILE}"
grep -q '^            FieldDecl f$' "${AST_FILE}"
grep -q '^              BuiltinType float$' "${AST_FILE}"
grep -q '^            FieldDecl u$' "${AST_FILE}"
grep -q '^              BuiltinType unsigned int$' "${AST_FILE}"
grep -q '^              BuiltinType unsigned long long$' "${AST_FILE}"

echo "verified: ast preserves anonymous union locals and unsigned field types"
