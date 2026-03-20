#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/pragma_once_transitive.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"

PREPROCESSED_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.preprocessed.sy"
if [[ "$(grep -c '^int shared_value = 12;$' "${PREPROCESSED_FILE}")" -ne 1 ]]; then
    echo "error: transitive #pragma once header was emitted more than once" >&2
    cat "${PREPROCESSED_FILE}" >&2
    exit 1
fi

grep -q '^    return shared_value + from_left + from_right;$' "${PREPROCESSED_FILE}"

echo "verified: #pragma once suppresses duplicate transitive inclusion through multiple parents"
