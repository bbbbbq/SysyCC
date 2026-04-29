#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${SYSYCC_BUILD_DIR:-${PROJECT_ROOT}/build}"
ARTIFACT_BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/multiline_macro_comment_apostrophe.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
PREPROCESSED_FILE="${ARTIFACT_BUILD_DIR}/intermediate_results/${TEST_NAME}.preprocessed.sy"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse >/dev/null

assert_basic_frontend_outputs "${ARTIFACT_BUILD_DIR}" "${TEST_NAME}"
if grep -q '#undef' "${PREPROCESSED_FILE}"; then
    echo "error: #undef leaked into preprocessed output" >&2
    exit 1
fi
grep -q '"fortran"' "${PREPROCESSED_FILE}"

echo "verified: multiline macro continuation ignores apostrophes in comments"
