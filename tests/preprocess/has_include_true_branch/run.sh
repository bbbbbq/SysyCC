#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/has_include_true_branch.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
SYSTEM_DIR="${SCRIPT_DIR}/system_include"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" -isystem "${SYSTEM_DIR}" "${INPUT_FILE}" --dump-tokens --dump-parse

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"

PREPROCESSED_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.preprocessed.sy"
grep -q '^int local_result = 11;$' "${PREPROCESSED_FILE}"
grep -q '^int system_result = 22;$' "${PREPROCESSED_FILE}"

echo "verified: __has_include returns true when local or system headers can be resolved"
