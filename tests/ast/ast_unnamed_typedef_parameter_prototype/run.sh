#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ast_unnamed_typedef_parameter_prototype.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
AST_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.ast.txt"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ast \
    --stop-after=ast

assert_file_nonempty "${AST_FILE}"
grep -q '^  FunctionDecl strncasecmp$' "${AST_FILE}"
grep -q '^  FunctionDecl vprintf$' "${AST_FILE}"
grep -q '^      NamedType size_t$' "${AST_FILE}"
grep -q '^      NamedType va_list$' "${AST_FILE}"

echo "verified: ast preserves unnamed typedef-name prototype parameters"
