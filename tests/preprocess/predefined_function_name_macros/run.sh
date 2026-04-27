#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/predefined_function_name_macros.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" --stop-after=preprocess "${INPUT_FILE}" >/dev/null

grep -q 'return "";' \
    "${BUILD_DIR}/intermediate_results/${TEST_NAME}.preprocessed.sy"
! grep -q "__PRETTY_FUNCTION__" \
    "${BUILD_DIR}/intermediate_results/${TEST_NAME}.preprocessed.sy"

echo "verified: function-name predefined macros expand in system-header asserts"
