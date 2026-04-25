#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
CASE_BUILD_DIR="${SCRIPT_DIR}/build"
INPUT_FILE="${SCRIPT_DIR}/cli_depfile_md_targets.c"
LOCAL_HEADER_FILE="${SCRIPT_DIR}/cli_depfile_md_targets.h"
SYSTEM_HEADER_FILE="${SCRIPT_DIR}/sys/shim_depfile_md_targets.h"
OBJ_FILE="${CASE_BUILD_DIR}/cli_depfile_md_targets.o"
DEP_FILE="${CASE_BUILD_DIR}/deps/cli_depfile_md_targets.d"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
mkdir -p "${CASE_BUILD_DIR}"

"${BUILD_DIR}/compiler" \
    -c \
    -MD \
    -MF "${DEP_FILE}" \
    -MT custom-target \
    -MQ "quoted target" \
    -MP \
    -isystem "${SCRIPT_DIR}/sys" \
    -o "${OBJ_FILE}" \
    "${INPUT_FILE}"

assert_file_nonempty "${OBJ_FILE}"
assert_file_nonempty "${DEP_FILE}"
grep -Fq "custom-target quoted\\ target:" "${DEP_FILE}"
grep -Fq "${INPUT_FILE}" "${DEP_FILE}"
grep -Fq "${LOCAL_HEADER_FILE}" "${DEP_FILE}"
grep -Fq "${SYSTEM_HEADER_FILE}" "${DEP_FILE}"
grep -Fxq "${LOCAL_HEADER_FILE}:" "${DEP_FILE}"

echo "verified: -MD includes system headers and honors -MT/-MQ/-MP depfile target controls"
