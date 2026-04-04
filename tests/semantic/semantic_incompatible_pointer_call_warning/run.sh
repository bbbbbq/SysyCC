#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/semantic_incompatible_pointer_call_warning.sy"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

set +e
OUTPUT="$("${BUILD_DIR}/SysyCC" --dump-tokens --dump-parse --dump-ast \
    --stop-after=semantic "${INPUT_FILE}" 2>&1)"
RC=$?
set -e

if [[ ${RC} -ne 0 ]]; then
    echo "error: compiler unexpectedly failed for ${INPUT_FILE}" >&2
    echo "${OUTPUT}" >&2
    exit 1
fi

grep -Fq ":8:27: warning: function call argument uses incompatible pointer type" \
    <<<"${OUTPUT}"

echo "verified: incompatible pointer call arguments warn without failing semantic analysis"
