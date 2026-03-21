#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/long_macro_pipeline.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"

PREPROCESSED_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.preprocessed.sy"

grep -q '^int alpha_value = 6;$' "${PREPROCESSED_FILE}"
grep -q '^int beta_value = 4;$' "${PREPROCESSED_FILE}"
grep -q '^int gamma_value = ((alpha_value) + (beta_value));$' "${PREPROCESSED_FILE}"
grep -q '^int delta_value = ((gamma_value) + (3));$' "${PREPROCESSED_FILE}"
grep -q '^int condition_value = ((delta_value) + (1));$' "${PREPROCESSED_FILE}"
grep -q '^int fallback_value = ((condition_value) + (2));$' "${PREPROCESSED_FILE}"
grep -q '^    return fallback_value;$' "${PREPROCESSED_FILE}"

if grep -q '^int condition_value = 0;$' "${PREPROCESSED_FILE}"; then
    echo "error: false conditional branch leaked into long_macro_pipeline output" >&2
    cat "${PREPROCESSED_FILE}" >&2
    exit 1
fi

echo "verified: long macro pipeline preserves rescan, token pasting, multiline macros, and conditional selection"
