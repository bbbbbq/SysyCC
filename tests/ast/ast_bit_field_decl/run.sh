#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ast_bit_field_decl.sy"
AST_FILE="${BUILD_DIR}/intermediate_results/ast_bit_field_decl.ast.txt"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-ast

assert_file_nonempty "${AST_FILE}"
grep -q '^  StructDecl Flags$' "${AST_FILE}"
grep -q '^    FieldDecl value : 5$' "${AST_FILE}"
grep -q '^      QualifiedType volatile$' "${AST_FILE}"
grep -q '^    FieldDecl payload$' "${AST_FILE}"

echo "verified: ast preserves bit-field width and qualifiers"
