#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ast_gnu_attribute_prototype.sy"
AST_FILE="${BUILD_DIR}/intermediate_results/ast_gnu_attribute_prototype.ast.txt"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" --stop-after=ast "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ast

grep -q '^  FunctionDecl foo$' "${AST_FILE}"
grep -q '^    Attributes$' "${AST_FILE}"
grep -q '^      Attribute __always_inline__$' "${AST_FILE}"

echo "verified: ast preserves GNU attribute lists on function prototypes"
