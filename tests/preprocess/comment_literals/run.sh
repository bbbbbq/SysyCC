#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/comment_literals.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"

grep -F -q '"http://example.com//not-comment"' "${BUILD_DIR}/intermediate_results/${TEST_NAME}.preprocessed.sy"
grep -F -q '"/*still a string literal*/"' "${BUILD_DIR}/intermediate_results/${TEST_NAME}.preprocessed.sy"
grep -F -q "StringLiteral \"http://example.com//not-comment\"" "${BUILD_DIR}/intermediate_results/${TEST_NAME}.tokens.txt"

echo "verified: comment stripping preserves literal contents"
