#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/semantic_logical_source_file.sy"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

set +e
OUTPUT="$("${BUILD_DIR}/SysyCC" "${INPUT_FILE}" 2>&1)"
RC=$?
set -e

if [[ ${RC} -eq 0 ]]; then
    echo "error: compiler unexpectedly succeeded" >&2
    exit 1
fi

if [[ "${OUTPUT}" != *"semantic error: undefined identifier: missing at virtual_semantic.sy:51:12-51:18"* ]]; then
    echo "error: semantic diagnostic did not use logical #line location" >&2
    echo "${OUTPUT}" >&2
    exit 1
fi

echo "verified: semantic diagnostics inherit logical file and line mapping from preprocess output"
