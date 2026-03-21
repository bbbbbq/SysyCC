#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/semantic_var_initializer_type.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

set +e
OUTPUT="$("${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse 2>&1)"
EXIT_CODE=$?
set -e

if [[ "${EXIT_CODE}" -eq 0 ]]; then
    echo "error: compiler unexpectedly succeeded for ${INPUT_FILE}" >&2
    exit 1
fi

if [[ "${OUTPUT}" != *"semantic error: initializer type does not match declared type"* ]]; then
    echo "error: expected semantic diagnostic not found" >&2
    echo "${OUTPUT}" >&2
    exit 1
fi

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"

echo "verified: semantic analysis checks variable initializer types"
