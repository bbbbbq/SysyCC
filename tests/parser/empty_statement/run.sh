#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/empty_statement.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse \
    --stop-after=parse

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"

grep -q '^    ;$' "${BUILD_DIR}/intermediate_results/${TEST_NAME}.preprocessed.sy"
grep -q '^        ;$' "${BUILD_DIR}/intermediate_results/${TEST_NAME}.preprocessed.sy"
if [[ "$(grep -c '^Semicolon ; ' "${BUILD_DIR}/intermediate_results/${TEST_NAME}.tokens.txt")" -lt 4 ]]; then
    echo "error: empty statements did not survive tokenization as standalone semicolons" >&2
    exit 1
fi

echo "verified: standalone empty statements parse cleanly in blocks and loops"
