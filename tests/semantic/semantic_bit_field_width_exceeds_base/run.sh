#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/semantic_bit_field_width_exceeds_base.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

set +e
OUTPUT="$("${BUILD_DIR}/SysyCC" --dump-tokens --dump-parse --stop-after=semantic \
    "${INPUT_FILE}" 2>&1)"
RC=$?
set -e

if [[ ${RC} -eq 0 ]]; then
    echo "error: compiler unexpectedly succeeded for ${INPUT_FILE}" >&2
    echo "${OUTPUT}" >&2
    exit 1
fi

grep -Fq "error: bit-field width exceeds base type width" <<<"${OUTPUT}"
assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"

echo "verified: semantic analysis rejects oversized bit-field widths"
