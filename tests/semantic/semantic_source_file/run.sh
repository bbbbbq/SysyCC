#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/semantic_source_file.sy"
PREPROCESSED_FILE="build/intermediate_results/semantic_source_file.preprocessed.sy"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

set +e
OUTPUT="$("${BUILD_DIR}/SysyCC" --stop-after=semantic "${INPUT_FILE}" 2>&1)"
RC=$?
set -e

if [[ ${RC} -eq 0 ]]; then
    echo "error: compiler unexpectedly succeeded" >&2
    exit 1
fi

if [[ "${OUTPUT}" != *"semantic error: undefined identifier: missing at ${INPUT_FILE}:2:12-2:18"* ]]; then
    echo "error: semantic diagnostic is missing source file path" >&2
    echo "${OUTPUT}" >&2
    exit 1
fi

echo "verified: semantic diagnostics include source file paths in source spans"
