#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/include_guard_reinclude.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"

PREPROCESSED_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.preprocessed.sy"

if [[ "$(grep -c '^int only_once = 11 + 31;$' "${PREPROCESSED_FILE}")" -ne 1 ]]; then
    echo "error: classic include guards did not suppress duplicate inclusion" >&2
    cat "${PREPROCESSED_FILE}" >&2
    exit 1
fi

grep -q '^    return only_once;$' "${PREPROCESSED_FILE}"

echo "verified: classic include guards suppress duplicate local inclusion and preserve nested definitions"
