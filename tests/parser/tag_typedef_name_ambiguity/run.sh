#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
INPUT_FILE="${SCRIPT_DIR}/${TEST_NAME}.sy"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" --stop-after=parse "${INPUT_FILE}" --dump-tokens --dump-parse

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"
grep -q 'STRUCT struct' "${BUILD_DIR}/intermediate_results/${TEST_NAME}.parse.txt"
grep -q 'TYPE_NAME Node' "${BUILD_DIR}/intermediate_results/${TEST_NAME}.parse.txt"
grep -q 'TYPE_NAME Box' "${BUILD_DIR}/intermediate_results/${TEST_NAME}.parse.txt"
grep -q 'TYPE_NAME Kind' "${BUILD_DIR}/intermediate_results/${TEST_NAME}.parse.txt"

echo "verified: parser accepts tag names that collide with typedef names"
