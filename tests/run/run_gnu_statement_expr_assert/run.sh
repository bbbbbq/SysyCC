#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${SYSYCC_BUILD_DIR:-${PROJECT_ROOT}/build}"
BINARY_FILE="${BUILD_DIR}/intermediate_results/run_gnu_statement_expr_assert"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
"${BUILD_DIR}/SysyCC" "${SCRIPT_DIR}/run_gnu_statement_expr_assert.sy" -o "${BINARY_FILE}"
"${BINARY_FILE}"

echo "verified: GNU __extension__ statement expression used by assert-style macros"
