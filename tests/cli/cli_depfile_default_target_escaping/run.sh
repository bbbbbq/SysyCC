#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
CASE_BUILD_DIR="${SCRIPT_DIR}/build"
INPUT_FILE="${SCRIPT_DIR}/cli_depfile_default_target_escaping.c"
HEADER_FILE="${SCRIPT_DIR}/local_header.h"
OBJ_FILE="${CASE_BUILD_DIR}/dir with spaces/output object.o"
DEP_FILE="${CASE_BUILD_DIR}/dir with spaces/output object.d"
ESCAPED_OBJ_FILE="${OBJ_FILE// /\\ }"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
mkdir -p "$(dirname "${OBJ_FILE}")"

"${BUILD_DIR}/compiler" \
    -c \
    -MMD \
    -MP \
    -o "${OBJ_FILE}" \
    "${INPUT_FILE}"

assert_file_nonempty "${OBJ_FILE}"
assert_file_nonempty "${DEP_FILE}"
grep -Fq "${ESCAPED_OBJ_FILE}:" "${DEP_FILE}"
grep -Fq "${HEADER_FILE}" "${DEP_FILE}"
grep -Fxq "${HEADER_FILE}:" "${DEP_FILE}"

echo "verified: default depfile targets are make-escaped and -MP still emits phony local-header targets"
