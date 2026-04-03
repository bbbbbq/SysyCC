#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/restrict_pointer_parameter_prototype.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" --stop-after=parse "${INPUT_FILE}" --dump-tokens --dump-parse

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"
grep -q 'KwRestrict restrict' "${BUILD_DIR}/intermediate_results/${TEST_NAME}.tokens.txt"
grep -q 'KwRestrict __restrict' "${BUILD_DIR}/intermediate_results/${TEST_NAME}.tokens.txt"
grep -q 'KwRestrict __restrict__' "${BUILD_DIR}/intermediate_results/${TEST_NAME}.tokens.txt"
grep -q 'pointer_qualifier' "${BUILD_DIR}/intermediate_results/${TEST_NAME}.parse.txt"
grep -q 'RESTRICT restrict' "${BUILD_DIR}/intermediate_results/${TEST_NAME}.parse.txt"
grep -q 'RESTRICT __restrict' "${BUILD_DIR}/intermediate_results/${TEST_NAME}.parse.txt"
grep -q 'RESTRICT __restrict__' "${BUILD_DIR}/intermediate_results/${TEST_NAME}.parse.txt"

echo "verified: parser accepts pointer-side restrict spellings"
