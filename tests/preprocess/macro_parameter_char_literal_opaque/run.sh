#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${SYSYCC_BUILD_DIR:-${PROJECT_ROOT}/build}"
INPUT_FILE="${SCRIPT_DIR}/macro_parameter_char_literal_opaque.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
"${BUILD_DIR}/SysyCC" -E "${INPUT_FILE}" >/dev/null

PREPROCESSED_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.preprocessed.sy"
grep -Fq "int short_name = ('f');" "${PREPROCESSED_FILE}"
grep -Fq "int flags = (32);" "${PREPROCESSED_FILE}"

if grep -Fq "'PARSE_OPT_NOCOMPLETE'" "${PREPROCESSED_FILE}"; then
    echo "error: macro parameter substitution rewrote a character literal" >&2
    exit 1
fi

echo "verified: macro parameters do not rewrite character literals"
