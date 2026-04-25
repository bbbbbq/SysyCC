#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
CASE_BUILD_DIR="${SCRIPT_DIR}/build"
INPUT_FILE="${SCRIPT_DIR}/cli_full_compile_external_link.c"
OUTPUT_FILE="${CASE_BUILD_DIR}/cli_full_compile_external_link.bin"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
mkdir -p "${CASE_BUILD_DIR}"
rm -f "${OUTPUT_FILE}"

"${BUILD_DIR}/compiler" -o "${OUTPUT_FILE}" "${INPUT_FILE}"

assert_file_nonempty "${OUTPUT_FILE}"

set +e
"${OUTPUT_FILE}"
PROGRAM_RC=$?
set -e

if [[ "${PROGRAM_RC}" -ne 17 ]]; then
    echo "error: full-compile executable returned ${PROGRAM_RC}, expected 17" >&2
    exit 1
fi

echo "verified: bare public driver invocations now lower to LLVM IR and use the host C driver to link an executable"
