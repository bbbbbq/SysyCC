#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/literal_formats.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"

grep -q '^IntLiteral 012 ' "${BUILD_DIR}/intermediate_results/${TEST_NAME}.tokens.txt"
grep -q '^IntLiteral 0x10 ' "${BUILD_DIR}/intermediate_results/${TEST_NAME}.tokens.txt"
grep -q '^FloatLiteral 0x1.0p-2f ' "${BUILD_DIR}/intermediate_results/${TEST_NAME}.tokens.txt"
grep -q '^FloatLiteral 0x1.8p+1 ' "${BUILD_DIR}/intermediate_results/${TEST_NAME}.tokens.txt"

echo "verified: lexer preserves octal, hexadecimal, and hexadecimal-float literal kinds"
