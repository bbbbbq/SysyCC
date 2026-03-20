#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/inactive_error_line_shield.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"

PREPROCESSED_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.preprocessed.sy"
grep -q '^int ok = 33;$' "${PREPROCESSED_FILE}"
grep -q '^    return ok;$' "${PREPROCESSED_FILE}"

if grep -q '^#line ' "${PREPROCESSED_FILE}"; then
    echo "error: inactive #line directive leaked into preprocessed output" >&2
    cat "${PREPROCESSED_FILE}" >&2
    exit 1
fi

echo "verified: inactive branches suppress #error and invalid #line directives entirely"
