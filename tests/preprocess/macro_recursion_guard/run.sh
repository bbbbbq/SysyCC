#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/macro_recursion_guard.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"

PREPROCESSED_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.preprocessed.sy"
grep -q '^int keep_self = SELF;$' "${PREPROCESSED_FILE}"
grep -q '^int keep_pair = FIRST;$' "${PREPROCESSED_FILE}"
grep -q '^int keep_call = LOOP(7);$' "${PREPROCESSED_FILE}"
grep -q '^    return keep_self + keep_pair + keep_call;$' "${PREPROCESSED_FILE}"

echo "verified: recursive macro definitions are suppressed instead of expanding forever"
