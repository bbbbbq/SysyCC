#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/semantic_array_dimension_constant.sy"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
assert_compiler_fails_with_message \
    "${BUILD_DIR}/SysyCC" --stop-after=semantic \
    "${INPUT_FILE}" \
    "semantic error: array dimension must be an integer constant expression"

echo "verified: semantic analysis checks array dimension constant expressions"
