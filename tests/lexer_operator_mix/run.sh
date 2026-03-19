#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/lexer_operator_mix.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"

TOKEN_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.tokens.txt"
grep -q '^Increment ++ ' "${TOKEN_FILE}"
grep -q '^Decrement -- ' "${TOKEN_FILE}"
grep -q '^BitAnd & ' "${TOKEN_FILE}"
grep -q '^BitNot ~ ' "${TOKEN_FILE}"
grep -q '^ShiftLeft << ' "${TOKEN_FILE}"
grep -q '^ShiftRight >> ' "${TOKEN_FILE}"
grep -q '^Arrow -> ' "${TOKEN_FILE}"

echo "verified: lexer preserves precise operator token kinds in mixed expressions"
