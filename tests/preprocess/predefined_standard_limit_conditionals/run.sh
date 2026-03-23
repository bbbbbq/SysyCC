#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/predefined_standard_limit_conditionals.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
PREPROCESSED_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.preprocessed.sy"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse >/dev/null 2>&1

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"
assert_file_nonempty "${PREPROCESSED_FILE}"
grep -q '^int int16_ge_int() {$' "${PREPROCESSED_FILE}"
grep -A1 '^int int16_ge_int() {$' "${PREPROCESSED_FILE}" | grep -q '^    return 0;$'
grep -q '^int int32_ge_int() {$' "${PREPROCESSED_FILE}"
grep -A1 '^int int32_ge_int() {$' "${PREPROCESSED_FILE}" | grep -q '^    return 1;$'
grep -q '^int char_bit_is_eight() {$' "${PREPROCESSED_FILE}"
grep -A1 '^int char_bit_is_eight() {$' "${PREPROCESSED_FILE}" | grep -q '^    return 1;$'

echo "verified: predefined standard integer limit conditionals evaluate like the host preprocessor"
