#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/long_include_graph.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
INCLUDE_B_DIR="${SCRIPT_DIR}/include_b"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" \
    -I "${INCLUDE_B_DIR}" \
    "${INPUT_FILE}" \
    --dump-tokens \
    --dump-parse

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"

PREPROCESSED_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.preprocessed.sy"

if [[ "$(grep -c '^int shared_once_value = 4;$' "${PREPROCESSED_FILE}")" -ne 1 ]]; then
    echo "error: transitive #pragma once failed inside long include graph" >&2
    cat "${PREPROCESSED_FILE}" >&2
    exit 1
fi

grep -q '^int from_left = 5;$' "${PREPROCESSED_FILE}"
grep -q '^int from_right = 9;$' "${PREPROCESSED_FILE}"
grep -q '^int nested_local_value = shared_once_value + 3;$' "${PREPROCESSED_FILE}"
grep -q '^int layered_total = 7 + 11 + 13;$' "${PREPROCESSED_FILE}"
grep -q '^int combined_total = shared_once_value + from_left + from_right + nested_local_value + layered_total;$' "${PREPROCESSED_FILE}"
grep -q '^    return combined_total;$' "${PREPROCESSED_FILE}"

echo "verified: long include graph preserves transitive pragma once, local include guards, and multi-hop quoted include_next resolution"
