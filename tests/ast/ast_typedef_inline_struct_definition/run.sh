#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ast_typedef_inline_struct_definition.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
AST_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.ast.txt"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
"${BUILD_DIR}/SysyCC" --stop-after=ast "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ast

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"
assert_file_nonempty "${AST_FILE}"
grep -q '^  StructDecl Pair$' "${AST_FILE}"
grep -q '^  TypedefDecl PairAlias$' "${AST_FILE}"

echo "verified: typedef inline struct definitions materialize both tag and typedef declarations"
