#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
CASE_BUILD_DIR="${SCRIPT_DIR}/build"
INPUT_FILE="${SCRIPT_DIR}/cli_depfile_output.c"
HEADER_FILE="${SCRIPT_DIR}/cli_depfile_output.h"
OBJ_FILE="${CASE_BUILD_DIR}/cli_depfile_output.o"
DEP_FILE="${CASE_BUILD_DIR}/deps/cli_depfile_output.d"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
mkdir -p "${CASE_BUILD_DIR}"

"${BUILD_DIR}/compiler" \
    -c \
    -MMD \
    -MF "${DEP_FILE}" \
    -MP \
    -o "${OBJ_FILE}" \
    "${INPUT_FILE}"

assert_file_nonempty "${OBJ_FILE}"
assert_file_nonempty "${DEP_FILE}"
grep -Fq "${OBJ_FILE}:" "${DEP_FILE}"
grep -Fq "${INPUT_FILE}" "${DEP_FILE}"
grep -Fq "${HEADER_FILE}" "${DEP_FILE}"
grep -Fxq "${HEADER_FILE}:" "${DEP_FILE}"

echo "verified: -MMD -MF -MP emits a depfile that tracks the primary output and local headers"
