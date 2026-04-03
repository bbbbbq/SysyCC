#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ast_pointer_types.sy"
AST_FILE="${BUILD_DIR}/intermediate_results/ast_pointer_types.ast.txt"
TEST_NAME="$(basename "${SCRIPT_DIR}")"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" --stop-after=ast "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ast

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"
assert_file_nonempty "${AST_FILE}"

grep -q '^  TypedefDecl IntPtr$' "${AST_FILE}"
grep -q '^    PointerType$' "${AST_FILE}"
grep -q '^      BuiltinType int$' "${AST_FILE}"
grep -q '^    FieldDecl next$' "${AST_FILE}"
grep -q '^      PointerType$' "${AST_FILE}"
grep -q '^        StructType Node$' "${AST_FILE}"
grep -q '^    ParamDecl input$' "${AST_FILE}"
grep -q '^      PointerType$' "${AST_FILE}"
grep -q '^        BuiltinType int$' "${AST_FILE}"
grep -q '^        VarDecl ptr$' "${AST_FILE}"
grep -q '^          PointerType$' "${AST_FILE}"

echo "verified: ast dump preserves pointer declarators in typedefs, fields, parameters, and variables"
