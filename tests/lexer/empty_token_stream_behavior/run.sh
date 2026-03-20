#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/empty_token_stream_behavior.sy"
RESULT_FILE="${BUILD_DIR}/intermediate_results/empty_token_stream_behavior.tokens.txt"

cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}"
cmake --build "${BUILD_DIR}"

set +e
OUTPUT="$("${BUILD_DIR}/SysyCC" --dump-tokens --dump-parse "${INPUT_FILE}" 2>&1)"
RC=$?
set -e

if [[ ${OUTPUT} == *"lexer produced no tokens"* ]]; then
    echo "error: lexer still rejects empty token streams" >&2
    echo "${OUTPUT}" >&2
    exit 1
fi

if [[ ! -f "${RESULT_FILE}" ]]; then
    echo "error: empty token stream did not produce a token dump file" >&2
    exit 1
fi

if [[ ${RC} -eq 0 ]]; then
    echo "verified: empty token stream no longer fails inside lexer"
    exit 0
fi

echo "verified: empty token stream no longer fails inside lexer"
