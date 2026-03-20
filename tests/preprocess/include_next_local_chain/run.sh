#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/include_next_local_chain.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
SECOND_DIR="${SCRIPT_DIR}/second_include_dir"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" \
    -I "${SECOND_DIR}" \
    "${INPUT_FILE}" \
    --dump-tokens \
    --dump-parse

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"

PREPROCESSED_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.preprocessed.sy"
grep -q '^int combined = 5 + 8;$' "${PREPROCESSED_FILE}"
grep -q '^    return combined;$' "${PREPROCESSED_FILE}"

echo "verified: quoted #include_next resumes lookup after the current local header"
