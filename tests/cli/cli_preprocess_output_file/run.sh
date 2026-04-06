#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/cli_preprocess_output_file.sy"
INCLUDE_DIR="${SCRIPT_DIR}/includes"
OUTPUT_FILE="${BUILD_DIR}/cli_preprocess_output_file.out"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
rm -f "${OUTPUT_FILE}"

set +e
STDOUT_CAPTURE="$("${BUILD_DIR}/SysyCC" \
    -E \
    -DCLI_MACRO=9 \
    -include force.h \
    -I "${INCLUDE_DIR}" \
    -nostdinc \
    -o "${OUTPUT_FILE}" \
    "${INPUT_FILE}" 2>&1)"
RC=$?
set -e

if [[ ${RC} -ne 0 ]]; then
    echo "error: -E -o unexpectedly failed" >&2
    echo "${STDOUT_CAPTURE}" >&2
    exit 1
fi

if [[ -n "${STDOUT_CAPTURE}" ]]; then
    echo "error: -E -o should not emit preprocessed text to stdout" >&2
    echo "${STDOUT_CAPTURE}" >&2
    exit 1
fi

grep -Fq "int macro_seen = 9;" "${OUTPUT_FILE}"
grep -Fq "int forced_seen = 24;" "${OUTPUT_FILE}"

echo "verified: -E writes the primary preprocessed output to -o"
