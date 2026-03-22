#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/pointer_nullability_parameter_prototype.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"
grep -q 'KwNullability _Nullable' "${BUILD_DIR}/intermediate_results/${TEST_NAME}.tokens.txt"
grep -q 'KwNullability _Nonnull' "${BUILD_DIR}/intermediate_results/${TEST_NAME}.tokens.txt"
grep -q 'KwNullability _Null_unspecified' "${BUILD_DIR}/intermediate_results/${TEST_NAME}.tokens.txt"
grep -q 'pointer_qualifier' "${BUILD_DIR}/intermediate_results/${TEST_NAME}.parse.txt"
grep -q 'NULLABILITY _Nullable' "${BUILD_DIR}/intermediate_results/${TEST_NAME}.parse.txt"
grep -q 'NULLABILITY _Nonnull' "${BUILD_DIR}/intermediate_results/${TEST_NAME}.parse.txt"
grep -q 'NULLABILITY _Null_unspecified' "${BUILD_DIR}/intermediate_results/${TEST_NAME}.parse.txt"

echo "verified: parser accepts pointer-side nullability spellings in anonymous function-pointer parameters"
