#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
CASE_BUILD_DIR="${SCRIPT_DIR}/build"
INPUT_FILE="${SCRIPT_DIR}/cli_depfile_matrix.c"
LOCAL_HEADER_FILE="${SCRIPT_DIR}/local_dep.h"
SYSTEM_HEADER_FILE="${SCRIPT_DIR}/sys/system_dep.h"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

cleanup() {
    rm -rf "${CASE_BUILD_DIR}"
}
trap cleanup EXIT

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
cleanup
mkdir -p "${CASE_BUILD_DIR}"

REL_DEP_FILE="build/nested/relative/matrix relative.d"
REL_LL_FILE="${CASE_BUILD_DIR}/matrix-relative.ll"

(
    cd "${SCRIPT_DIR}"
    "${BUILD_DIR}/compiler" \
        -S \
        -emit-llvm \
        -MMD \
        "-MF${REL_DEP_FILE}" \
        -MT "raw target" \
        -MQ "quoted target" \
        -MT 'literal$target' \
        -MQ 'quoted$target' \
        -I "${SCRIPT_DIR}" \
        -isystem "${SCRIPT_DIR}/sys" \
        -o "${REL_LL_FILE}" \
        "${INPUT_FILE}"
)

REL_DEP_PATH="${SCRIPT_DIR}/${REL_DEP_FILE}"
assert_file_nonempty "${REL_LL_FILE}"
assert_file_nonempty "${REL_DEP_PATH}"
grep -Fq 'raw target quoted\ target literal$target quoted$$target:' "${REL_DEP_PATH}"
grep -Fq "${INPUT_FILE}" "${REL_DEP_PATH}"
grep -Fq "${LOCAL_HEADER_FILE}" "${REL_DEP_PATH}"
if grep -Fq "${SYSTEM_HEADER_FILE}" "${REL_DEP_PATH}"; then
    echo "error: -MMD depfile unexpectedly included an -isystem header" >&2
    cat "${REL_DEP_PATH}" >&2
    exit 1
fi

ABS_DEP_FILE="${CASE_BUILD_DIR}/absolute/deps/matrix-absolute.d"
ABS_LL_FILE="${CASE_BUILD_DIR}/absolute/matrix-absolute.ll"

"${BUILD_DIR}/compiler" \
    -S \
    -emit-llvm \
    -MD \
    -MF "${ABS_DEP_FILE}" \
    -MQ "${ABS_LL_FILE}" \
    -MP \
    -I "${SCRIPT_DIR}" \
    -isystem "${SCRIPT_DIR}/sys" \
    -o "${ABS_LL_FILE}" \
    "${INPUT_FILE}"

assert_file_nonempty "${ABS_LL_FILE}"
assert_file_nonempty "${ABS_DEP_FILE}"
grep -Fq "${ABS_LL_FILE}:" "${ABS_DEP_FILE}"
grep -Fq "${INPUT_FILE}" "${ABS_DEP_FILE}"
grep -Fq "${LOCAL_HEADER_FILE}" "${ABS_DEP_FILE}"
grep -Fq "${SYSTEM_HEADER_FILE}" "${ABS_DEP_FILE}"
grep -Fxq "${LOCAL_HEADER_FILE}:" "${ABS_DEP_FILE}"
grep -Fxq "${SYSTEM_HEADER_FILE}:" "${ABS_DEP_FILE}"

echo "verified: depfile matrix covers -MD/-MMD, nested relative and absolute -MF paths, -MT raw targets, -MQ make escaping, ordered multi-target output, and -MP phony headers"
