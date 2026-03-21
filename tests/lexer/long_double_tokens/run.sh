#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/long_double_tokens.sy"
TOKENS_FILE="${BUILD_DIR}/intermediate_results/long_double_tokens.tokens.txt"
TEST_NAME="$(basename "${SCRIPT_DIR}")"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse >/tmp/sysycc_long_double_tokens.out 2>&1

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"
grep -q '^KwExtern extern ' "${TOKENS_FILE}"
grep -q '^KwLong long ' "${TOKENS_FILE}"
grep -q '^KwDouble double ' "${TOKENS_FILE}"

echo "verified: lexer recognizes long double keyword sequence"
