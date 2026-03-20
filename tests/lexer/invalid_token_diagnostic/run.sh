#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/invalid_token_diagnostic.sy"

cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}"
cmake --build "${BUILD_DIR}"

set +e
OUTPUT="$("${BUILD_DIR}/SysyCC" --dump-tokens "${INPUT_FILE}" 2>&1)"
RC=$?
set -e

if [[ ${RC} -eq 0 ]]; then
    echo "error: invalid token input unexpectedly succeeded" >&2
    exit 1
fi

if [[ "${OUTPUT}" != *"lexer encountered invalid token '@' at 2:12-2:12"* ]]; then
    echo "error: invalid token diagnostic is missing lexeme or source span" >&2
    echo "${OUTPUT}" >&2
    exit 1
fi

echo "verified: invalid token diagnostics include lexeme and source span"
