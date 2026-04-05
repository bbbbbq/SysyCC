#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/semantic_narrowing_call_argument_warning.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

set +e
OUTPUT="$("${BUILD_DIR}/SysyCC" -Wconversion --stop-after=semantic "${INPUT_FILE}" --dump-tokens --dump-parse 2>&1)"
RC=$?
set -e

if [[ ${RC} -ne 0 ]]; then
    echo "error: compiler unexpectedly failed for ${INPUT_FILE}" >&2
    echo "${OUTPUT}" >&2
    exit 1
fi

grep -Fq "warning: implicit integer conversion may change value" <<<"${OUTPUT}"
assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"

echo "verified: semantic analysis warns on narrowing integer call arguments"
