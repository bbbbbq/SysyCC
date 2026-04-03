#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ast_type_decls.sy"
AST_FILE="${BUILD_DIR}/intermediate_results/ast_type_decls.ast.txt"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
"${BUILD_DIR}/SysyCC" --stop-after=ast --dump-tokens --dump-parse --dump-ast "${INPUT_FILE}" >/tmp/sysycc_ast_type_decls.out 2>&1

grep -q "^  StructDecl Pair$" "${AST_FILE}"
grep -q "^  EnumDecl Flag$" "${AST_FILE}"
grep -q "^  TypedefDecl Size$" "${AST_FILE}"
grep -q "^    FieldDecl x$" "${AST_FILE}"
grep -q "^    EnumeratorDecl ZERO$" "${AST_FILE}"

if grep -q "UnknownDecl" "${AST_FILE}"; then
    echo "error: type declarations still lower to UnknownDecl" >&2
    exit 1
fi

if grep -q "UnknownType struct_specifier" "${AST_FILE}"; then
    echo "error: struct type still lowers to UnknownType" >&2
    exit 1
fi

if grep -q "UnknownType enum_specifier" "${AST_FILE}"; then
    echo "error: enum type still lowers to UnknownType" >&2
    exit 1
fi

echo "verified: ast dump lowers struct, enum, and typedef declarations"
