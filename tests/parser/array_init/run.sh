#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/array_init.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
PREPROCESSED_FILE="build/intermediate_results/${TEST_NAME}.preprocessed.sy"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"

grep -q "^KwConst const ${INPUT_FILE}:1:1-1:5$" "${BUILD_DIR}/intermediate_results/${TEST_NAME}.tokens.txt"
grep -q "^Identifier size ${INPUT_FILE}:1:11-1:14$" "${BUILD_DIR}/intermediate_results/${TEST_NAME}.tokens.txt"
grep -q '^                                                      INT_LITERAL 3$' "${BUILD_DIR}/intermediate_results/${TEST_NAME}.parse.txt"

echo "verified: array initialization frontend artifacts are generated correctly"
