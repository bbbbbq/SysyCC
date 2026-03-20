#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/bitwise_conditional_expr.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"

PREPROCESSED_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.preprocessed.sy"
grep -q '^int bitand_branch() {$' "${PREPROCESSED_FILE}"
grep -q '^int bitor_branch() {$' "${PREPROCESSED_FILE}"
grep -q '^int bitxor_branch() {$' "${PREPROCESSED_FILE}"
grep -q '^int shr_branch() {$' "${PREPROCESSED_FILE}"
grep -q '^int shl_branch() {$' "${PREPROCESSED_FILE}"
grep -q '^int bitnot_branch() {$' "${PREPROCESSED_FILE}"
grep -q '^    return 1;$' "${PREPROCESSED_FILE}"
grep -q '^    return 2;$' "${PREPROCESSED_FILE}"
grep -q '^    return 4;$' "${PREPROCESSED_FILE}"
grep -q '^    return 8;$' "${PREPROCESSED_FILE}"
grep -q '^    return 16;$' "${PREPROCESSED_FILE}"
grep -q '^    return 32;$' "${PREPROCESSED_FILE}"

echo "verified: #if expressions support bitwise and shift operators"
