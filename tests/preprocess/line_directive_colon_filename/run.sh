#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/line_directive_colon_filename.sy"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

set +e
OUTPUT="$("${BUILD_DIR}/SysyCC" "${INPUT_FILE}" 2>&1)"
EXIT_CODE=$?
set -e

if [[ "${EXIT_CODE}" -eq 0 ]]; then
    echo "error: compiler unexpectedly succeeded for ${INPUT_FILE}" >&2
    exit 1
fi

grep -Fq "virtual:input.sy:200:1: error: invalid defined() operand in #if expression" \
    <<<"${OUTPUT}"

echo "verified: preprocess diagnostics preserve GCC-like headers for logical file names containing colons"
