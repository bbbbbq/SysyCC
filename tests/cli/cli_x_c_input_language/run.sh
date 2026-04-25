#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
CASE_BUILD_DIR="${SCRIPT_DIR}/build"
INPUT_FILE="${SCRIPT_DIR}/cli_x_c_input_language.txt"
OBJ_FILE="${CASE_BUILD_DIR}/cli_x_c_input_language.o"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
mkdir -p "${CASE_BUILD_DIR}"

"${BUILD_DIR}/compiler" -x c -c -o "${OBJ_FILE}" "${INPUT_FILE}"

assert_file_nonempty "${OBJ_FILE}"

echo "verified: -x c accepts non-.c source paths on the public compile-only driver"
