#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/include_error_trace.sy"

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

grep -Fq "${SCRIPT_DIR}/level2.h:1:1: error: failed to resolve included file: missing.h" <<<"${OUTPUT}"
grep -Fq "  #include \"missing.h\"" <<<"${OUTPUT}"
grep -Fq "${SCRIPT_DIR}/level1.h:1:1: note: included from here" <<<"${OUTPUT}"
grep -Fq "${SCRIPT_DIR}/include_error_trace.sy:1:1: note: included from here" <<<"${OUTPUT}"

echo "verified: preprocess include errors carry GCC-like headers and nested include notes"
