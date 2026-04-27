#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/semantic_function_pointer_return_pointer_typedef.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
"${BUILD_DIR}/SysyCC" --stop-after=semantic "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ast
assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"

grep -Fq "FunctionType" "${BUILD_DIR}/intermediate_results/${TEST_NAME}.ast.txt"
grep -Fq "PointerType" "${BUILD_DIR}/intermediate_results/${TEST_NAME}.ast.txt"

echo "verified: function-pointer typedefs can return pointer types"
