#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/parser_error_diagnostic.sy"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

set +e
OUTPUT="$("${BUILD_DIR}/SysyCC" --stop-after=parse "${INPUT_FILE}" 2>&1)"
EXIT_CODE=$?
set -e

if [[ "${EXIT_CODE}" -eq 0 ]]; then
    echo "error: compiler unexpectedly succeeded for ${INPUT_FILE}" >&2
    exit 1
fi

grep -Fq "${INPUT_FILE}:1:11: error: syntax error near '{'" <<<"${OUTPUT}"
grep -Fq "  int main( {" <<<"${OUTPUT}"
grep -Eq '^[[:space:]]+\^$' <<<"${OUTPUT}"

echo "verified: parser diagnostics use GCC-like headers and caret output"
