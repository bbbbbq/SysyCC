#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/invalid_macro_name_bug.sy"

cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}"
cmake --build "${BUILD_DIR}"

set +e
OUTPUT="$("${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse 2>&1)"
STATUS=$?
set -e

if [[ ${STATUS} -eq 0 ]]; then
    echo "error: invalid macro name was still accepted" >&2
    exit 1
fi

if [[ "${OUTPUT}" != *"invalid #define directive: invalid macro name"* ]]; then
    echo "error: invalid macro name did not produce the expected diagnostic" >&2
    echo "${OUTPUT}" >&2
    exit 1
fi

echo "verified: invalid macro names are rejected explicitly"
echo "${OUTPUT}"
