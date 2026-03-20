#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/pragma_once.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" \
    "${INPUT_FILE}" \
    --dump-tokens \
    --dump-parse

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"

PREPROCESSED_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.preprocessed.sy"

if [[ "$(grep -c '^int helper_value = 7;$' "${PREPROCESSED_FILE}")" -ne 1 ]]; then
    echo "error: #pragma once did not suppress repeated inclusion" >&2
    cat "${PREPROCESSED_FILE}" >&2
    exit 1
fi

echo "verified: #pragma once suppresses repeated inclusion and unknown pragmas are ignored"
