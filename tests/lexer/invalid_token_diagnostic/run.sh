#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/invalid_token_diagnostic.sy"
PREPROCESSED_FILE="build/intermediate_results/invalid_token_diagnostic.preprocessed.sy"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

set +e
OUTPUT="$("${BUILD_DIR}/SysyCC" --dump-tokens "${INPUT_FILE}" 2>&1)"
RC=$?
set -e

if [[ ${RC} -eq 0 ]]; then
    echo "error: invalid token input unexpectedly succeeded" >&2
    exit 1
fi

grep -Fq "${INPUT_FILE}:2:12: error: invalid token '@'" <<<"${OUTPUT}"
grep -Fq "      return @;" <<<"${OUTPUT}"
grep -Eq '^[[:space:]]+\^$' <<<"${OUTPUT}"

echo "verified: invalid token diagnostics use GCC-like headers and caret output"
