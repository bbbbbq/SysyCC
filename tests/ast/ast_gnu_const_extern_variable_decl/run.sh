#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ast_gnu_const_extern_variable_decl.sy"
AST_FILE="${BUILD_DIR}/intermediate_results/ast_gnu_const_extern_variable_decl.ast.txt"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ast

grep -q '^  VarDecl sys_nerr$' "${AST_FILE}"
grep -q '^    QualifiedType const$' "${AST_FILE}"
grep -q '^      BuiltinType int$' "${AST_FILE}"
grep -q '^  VarDecl sys_errlist$' "${AST_FILE}"
grep -q '^    PointerType$' "${AST_FILE}"
grep -q '^      PointerQualifiers const$' "${AST_FILE}"
grep -q '^      QualifiedType const$' "${AST_FILE}"

echo "verified: ast preserves extern GNU-const variable declarations"
