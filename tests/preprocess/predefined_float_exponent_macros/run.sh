#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/predefined_float_exponent_macros.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
PREPROCESSED_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.preprocessed.sy"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse >/dev/null 2>&1

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"
assert_file_nonempty "${PREPROCESSED_FILE}"
! grep -q '__DBL_MAX_10_EXP__' "${PREPROCESSED_FILE}"
! grep -q '__DBL_MIN_10_EXP__' "${PREPROCESSED_FILE}"
! grep -q '__FLT_MAX_EXP__' "${PREPROCESSED_FILE}"
! grep -q '__LDBL_MIN_EXP__' "${PREPROCESSED_FILE}"

echo "verified: predefined floating exponent macros expand during preprocessing"
