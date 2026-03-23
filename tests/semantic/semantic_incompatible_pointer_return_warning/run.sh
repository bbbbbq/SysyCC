#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/semantic_incompatible_pointer_return_warning.sy"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

set +e
OUTPUT="$("${BUILD_DIR}/SysyCC" --stop-after=semantic "${INPUT_FILE}" 2>&1)"
RC=$?
set -e

if [[ ${RC} -ne 0 ]]; then
    echo "error: compiler unexpectedly failed for ${INPUT_FILE}" >&2
    echo "${OUTPUT}" >&2
    exit 1
fi

NORMALIZED_OUTPUT="$(printf '%s' "${OUTPUT}" | sed -E 's#[^[:space:]]+:([0-9]+:[0-9]+-[0-9]+:[0-9]+)#\1#g')"
grep -Fq "semantic warning: return between incompatible pointer types at 5:5-5:17" \
    <<<"${NORMALIZED_OUTPUT}"

echo "verified: incompatible pointer returns warn without failing semantic analysis"
