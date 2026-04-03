#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ast_stmt_extensions.sy"
AST_FILE="${BUILD_DIR}/intermediate_results/ast_stmt_extensions.ast.txt"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
"${BUILD_DIR}/SysyCC" --stop-after=ast --dump-tokens --dump-parse --dump-ast "${INPUT_FILE}" >/tmp/sysycc_ast_stmt_extensions.out 2>&1

grep -q "ForStmt" "${AST_FILE}"
grep -q "DoWhileStmt" "${AST_FILE}"
grep -q "SwitchStmt" "${AST_FILE}"
grep -q "CaseStmt" "${AST_FILE}"
grep -q "DefaultStmt" "${AST_FILE}"
grep -q "BreakStmt" "${AST_FILE}"

if grep -q "UnknownStmt" "${AST_FILE}"; then
    echo "error: extended statements still lower to UnknownStmt" >&2
    exit 1
fi

echo "verified: ast dump lowers extended statement forms"
