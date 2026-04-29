#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/leading_static_mid_gnu_attribute_function.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
PARSE_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.parse.txt"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-parse >/dev/null

assert_file_nonempty "${PARSE_FILE}"
grep -q 'ATTRIBUTE __attribute__' "${PARSE_FILE}"

echo "verified: leading plus mid GNU attributes around static functions parse"
