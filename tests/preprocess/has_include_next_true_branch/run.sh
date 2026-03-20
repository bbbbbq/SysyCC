#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/has_include_next_true_branch.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
FIRST_SYSTEM_DIR="${SCRIPT_DIR}/system_first"
SECOND_SYSTEM_DIR="${SCRIPT_DIR}/system_second"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" \
    -isystem "${FIRST_SYSTEM_DIR}" \
    -isystem "${SECOND_SYSTEM_DIR}" \
    "${INPUT_FILE}" \
    --dump-tokens \
    --dump-parse

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"

PREPROCESSED_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.preprocessed.sy"
grep -q '^int next_result = 33;$' "${PREPROCESSED_FILE}"

echo "verified: __has_include_next returns true when the next system include directory contains the header"
