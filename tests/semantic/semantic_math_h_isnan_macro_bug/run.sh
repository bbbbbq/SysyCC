#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/semantic_math_h_isnan_macro_bug.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
PREPROCESSED_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.preprocessed.sy"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" --stop-after=semantic --dump-tokens --dump-parse "${INPUT_FILE}"

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"
assert_file_nonempty "${PREPROCESSED_FILE}"

grep -q "__inline_isnanf" "${PREPROCESSED_FILE}"
grep -q "sizeof(float)" "${PREPROCESSED_FILE}"

echo "verified: semantic analysis accepts math.h isnan macro expansions"
