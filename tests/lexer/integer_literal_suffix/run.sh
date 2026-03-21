#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/integer_literal_suffix.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
TOKEN_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.tokens.txt"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"

grep -q "^IntLiteral 0x2AUL ${INPUT_FILE}:2:12-2:17$" "${TOKEN_FILE}"
grep -q "^IntLiteral 7u ${INPUT_FILE}:2:21-2:22$" "${TOKEN_FILE}"
grep -q "^IntLiteral 010LL ${INPUT_FILE}:2:26-2:30$" "${TOKEN_FILE}"

echo "verified: lexer accepts integer literals with standard unsigned/long suffixes"
