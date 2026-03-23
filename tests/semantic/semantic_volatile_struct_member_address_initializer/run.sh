#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/semantic_volatile_struct_member_address_initializer.sy"

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

if grep -Fq "semantic error:" <<<"${OUTPUT}"; then
    echo "error: unexpected semantic error for volatile struct member address initializer" >&2
    echo "${OUTPUT}" >&2
    exit 1
fi

echo "verified: volatile aggregate qualifiers propagate to member address initializers"
