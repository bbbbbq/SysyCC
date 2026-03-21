#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/c_token_kinds.sy"
TOKENS_FILE="${BUILD_DIR}/intermediate_results/c_token_kinds.tokens.txt"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
"${BUILD_DIR}/SysyCC" --dump-tokens --dump-parse "${INPUT_FILE}" >/tmp/sysycc_c_token_kinds.out 2>&1

assert_file_nonempty "${TOKENS_FILE}"

grep -q '^KwTypedef typedef ' "${TOKENS_FILE}"
grep -q '^KwStruct struct ' "${TOKENS_FILE}"
grep -q '^KwEnum enum ' "${TOKENS_FILE}"
grep -q '^KwFloat float ' "${TOKENS_FILE}"
grep -q '^FloatLiteral 1.5 ' "${TOKENS_FILE}"
grep -q '^StringLiteral "hello" ' "${TOKENS_FILE}"
grep -q "^CharLiteral 'x' " "${TOKENS_FILE}"

echo "verified: token dumps preserve C-style keyword, float, string, and char token kinds"
