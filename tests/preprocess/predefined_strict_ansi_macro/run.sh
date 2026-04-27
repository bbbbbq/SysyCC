#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/predefined_strict_ansi_macro.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
PREPROCESSED_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.preprocessed.sy"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
"${BUILD_DIR}/SysyCC" -std=c99 "${INPUT_FILE}" --dump-tokens --dump-parse >/dev/null

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"
assert_file_nonempty "${PREPROCESSED_FILE}"
grep -q '^int strict_ansi_enabled = 1;$' "${PREPROCESSED_FILE}"

echo "verified: strict C99 preprocessing defines __STRICT_ANSI__"
