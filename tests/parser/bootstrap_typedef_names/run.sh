#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/bootstrap_typedef_names.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" --stop-after=parse "${INPUT_FILE}" --dump-tokens --dump-parse

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"
grep -q 'TYPE_NAME size_t' "${BUILD_DIR}/intermediate_results/${TEST_NAME}.parse.txt"
grep -q 'TYPE_NAME ptrdiff_t' "${BUILD_DIR}/intermediate_results/${TEST_NAME}.parse.txt"
grep -q 'TYPE_NAME va_list' "${BUILD_DIR}/intermediate_results/${TEST_NAME}.parse.txt"
grep -q 'TYPE_NAME wchar_t' "${BUILD_DIR}/intermediate_results/${TEST_NAME}.parse.txt"
grep -q 'TYPE_NAME uint32_t' "${BUILD_DIR}/intermediate_results/${TEST_NAME}.parse.txt"
grep -q 'TYPE_NAME int32_t' "${BUILD_DIR}/intermediate_results/${TEST_NAME}.parse.txt"
grep -q 'TYPE_NAME uint64_t' "${BUILD_DIR}/intermediate_results/${TEST_NAME}.parse.txt"
grep -q 'TYPE_NAME intptr_t' "${BUILD_DIR}/intermediate_results/${TEST_NAME}.parse.txt"
grep -q 'TYPE_NAME uintptr_t' "${BUILD_DIR}/intermediate_results/${TEST_NAME}.parse.txt"

echo "verified: parser bootstraps standard/toolchain typedef-names"
