#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/c_parser_extensions.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse \
    --stop-after=parse

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"

grep -q '^KwStruct struct ' "${BUILD_DIR}/intermediate_results/${TEST_NAME}.tokens.txt"
grep -q '^KwSwitch switch ' "${BUILD_DIR}/intermediate_results/${TEST_NAME}.tokens.txt"

echo "verified: c-style parser extension tokens and parse artifacts are generated correctly"
