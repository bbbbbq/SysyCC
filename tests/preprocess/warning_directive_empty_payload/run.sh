#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/warning_directive_empty_payload.sy"
PREPROCESSED_FILE="${BUILD_DIR}/intermediate_results/warning_directive_empty_payload.preprocessed.sy"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

set +e
OUTPUT="$("${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse 2>&1)"
RC=$?
set -e

if [[ ${RC} -ne 0 ]]; then
    echo "error: empty #warning input unexpectedly failed" >&2
    echo "${OUTPUT}" >&2
    exit 1
fi

assert_file_nonempty "${PREPROCESSED_FILE}"
grep -q '^int warning_directive_empty_payload_value = 11;$' "${PREPROCESSED_FILE}"
grep -Fq "${INPUT_FILE}:1:1: warning: #warning directive triggered" <<<"${OUTPUT}"

echo "verified: empty #warning directives use the default warning message"
