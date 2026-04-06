#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/cli_preprocess_stdout.sy"
INCLUDE_DIR="${SCRIPT_DIR}/includes"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

set +e
OUTPUT="$("${BUILD_DIR}/compiler" \
    -E \
    -DCLI_MACRO=7 \
    -DHIDDEN \
    -UHIDDEN \
    -include force.h \
    -I "${INCLUDE_DIR}" \
    -nostdinc \
    "${INPUT_FILE}" 2>&1)"
RC=$?
set -e

if [[ ${RC} -ne 0 ]]; then
    echo "error: -E unexpectedly failed" >&2
    echo "${OUTPUT}" >&2
    exit 1
fi

grep -Fq "int macro_seen = 7;" <<<"${OUTPUT}"
grep -Fq "int forced_seen = 42;" <<<"${OUTPUT}"
if grep -Fq "int hidden = 1;" <<<"${OUTPUT}"; then
    echo "error: -U did not remove the macro definition" >&2
    echo "${OUTPUT}" >&2
    exit 1
fi

echo "verified: -E prints preprocessed text to stdout and honors -D/-U/-include/-nostdinc"
