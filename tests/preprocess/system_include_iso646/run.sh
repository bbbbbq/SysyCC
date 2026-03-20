#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/system_include_iso646.sy"
TEST_NAME="system_include_iso646"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"
grep -q 'return 1 && 1;' "${BUILD_DIR}/intermediate_results/${TEST_NAME}.preprocessed.sy"

echo "verified: system include resolves <iso646.h> and expands system macros"
