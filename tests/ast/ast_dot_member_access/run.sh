#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ast_dot_member_access.sy"
AST_FILE="${BUILD_DIR}/intermediate_results/ast_dot_member_access.ast.txt"
TEST_NAME="$(basename "${SCRIPT_DIR}")"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ast

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"
assert_file_nonempty "${AST_FILE}"

grep -q 'MemberExpr \. left' "${AST_FILE}"
grep -q 'IdentifierExpr pair' "${AST_FILE}"
grep -q 'AssignExpr' "${AST_FILE}"

echo "verified: ast dump lowers direct dot member access expressions"
