#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/include_angle_header_not_macro_expanded.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
PREPROCESSED_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.preprocessed.sy"
SYSTEM_DIR="${SCRIPT_DIR}/system_headers"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" -isystem "${SYSTEM_DIR}" \
    --dump-tokens --dump-parse >/dev/null

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"
assert_file_nonempty "${PREPROCESSED_FILE}"
grep -q "errno_header_value" "${PREPROCESSED_FILE}"

echo "verified: angle header names are not macro expanded by #include"
