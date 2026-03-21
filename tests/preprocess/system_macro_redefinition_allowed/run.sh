#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/system_macro_redefinition_allowed.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
SYSTEM_DIR="${SCRIPT_DIR}/system_headers"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" \
    -isystem "${SYSTEM_DIR}" \
    "${INPUT_FILE}" \
    --dump-tokens \
    --dump-parse >/dev/null 2>&1

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"

PREPROCESSED_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.preprocessed.sy"
grep -q '^    return 22;$' "${PREPROCESSED_FILE}"

echo "verified: conflicting macro redefinitions are tolerated across system headers"
