#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/line_directive_spaced_logical_location.sy"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

set +e
OUTPUT="$("${BUILD_DIR}/SysyCC" --dump-tokens "${INPUT_FILE}" 2>&1)"
RC=$?
set -e

if [[ ${RC} -eq 0 ]]; then
    echo "error: spaced line-directive lexer input unexpectedly succeeded" >&2
    exit 1
fi

if [[ "${OUTPUT}" != *"virtual lexer spaced.sy:41:12: error: invalid token '@'"* ]]; then
    echo "error: lexer diagnostic did not preserve spaced logical #line file name" >&2
    echo "${OUTPUT}" >&2
    exit 1
fi

echo "verified: lexer diagnostics preserve spaced logical file names from #line"
