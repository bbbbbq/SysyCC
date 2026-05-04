#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/run_sysy_float_literal_single_precision.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
IR_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.ll"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

BAKED_STANDARD="$(
    grep '^SYSYCC_SOURCE_LANGUAGE_STANDARD:STRING=' "${BUILD_DIR}/CMakeCache.txt" |
        sed 's/^SYSYCC_SOURCE_LANGUAGE_STANDARD:STRING=//' || true
)"
if [[ -z "${BAKED_STANDARD}" ]]; then
    BAKED_STANDARD="sysy22"
fi

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-ir
assert_file_nonempty "${IR_FILE}"
if [[ "${BAKED_STANDARD}" == "sysy22" ]]; then
    grep -q 'fdiv float ' "${IR_FILE}"
    if grep -q 'fdiv double ' "${IR_FILE}"; then
        echo "unexpected double arithmetic for baked SysY22 float literal semantics" >&2
        exit 1
    fi
else
    grep -q 'fdiv double ' "${IR_FILE}"
    grep -q 'fptrunc double ' "${IR_FILE}"
fi

"${BUILD_DIR}/SysyCC" -std=c99 "${INPUT_FILE}" --dump-ir
assert_file_nonempty "${IR_FILE}"
if [[ "${BAKED_STANDARD}" == "sysy22" ]]; then
    grep -q 'fdiv float ' "${IR_FILE}"
    if grep -q 'fdiv double ' "${IR_FILE}"; then
        echo "runtime -std=c99 unexpectedly changed baked SysY22 float literal semantics" >&2
        exit 1
    fi
else
    grep -q 'fdiv double ' "${IR_FILE}"
    grep -q 'fptrunc double ' "${IR_FILE}"
fi

echo "verified: baked ${BAKED_STANDARD} floating literal semantics are independent of runtime -std"
