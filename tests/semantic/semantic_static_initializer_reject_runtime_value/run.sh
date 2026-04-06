#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/semantic_static_initializer_reject_runtime_value.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

assert_compiler_fails_with_message \
    "${BUILD_DIR}/SysyCC" --stop-after=semantic --dump-tokens --dump-parse \
    "${INPUT_FILE}" \
    "semantic error: initializer is not a valid static initializer"

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"
echo "verified: semantic analysis rejects runtime-valued static initializers"
