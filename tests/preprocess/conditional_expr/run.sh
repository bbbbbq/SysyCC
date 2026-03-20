#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/conditional_expr.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"

PREPROCESSED_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.preprocessed.sy"
grep -q '^int foo_branch() {$' "${PREPROCESSED_FILE}"
grep -q '^int defined_branch() {$' "${PREPROCESSED_FILE}"
grep -q '^int and_branch() {$' "${PREPROCESSED_FILE}"
grep -q '^int arithmetic_branch() {$' "${PREPROCESSED_FILE}"

echo "verified: conditional preprocessing keeps the expected branches"
