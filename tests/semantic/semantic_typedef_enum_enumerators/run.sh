#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/semantic_typedef_enum_enumerators.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" -fsyntax-only --dump-ast

assert_file_nonempty "${BUILD_DIR}/intermediate_results/${TEST_NAME}.ast.txt"

grep -q 'EnumeratorDecl TM_N' "${BUILD_DIR}/intermediate_results/${TEST_NAME}.ast.txt"
grep -q 'EnumeratorDecl F2Ieq' "${BUILD_DIR}/intermediate_results/${TEST_NAME}.ast.txt"

echo "verified: typedef enum declarations publish enumerators"
