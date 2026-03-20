#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/precise_token_kinds.sy"
TOKENS_FILE="${BUILD_DIR}/intermediate_results/precise_token_kinds.tokens.txt"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
"${BUILD_DIR}/SysyCC" --dump-tokens --dump-parse "${INPUT_FILE}" >/tmp/sysycc_precise_token_kinds.out 2>&1

grep -q "^KwInt int " "${TOKENS_FILE}"
grep -q "^Identifier main " "${TOKENS_FILE}"
grep -q "^LParen ( " "${TOKENS_FILE}"
grep -q "^KwReturn return " "${TOKENS_FILE}"
grep -q "^IntLiteral 0 " "${TOKENS_FILE}"
grep -q "^Semicolon ; " "${TOKENS_FILE}"

echo "verified: token dumps preserve exact token kinds"
