#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/semantic_incompatible_pointer_call_warning.sy"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
assert_compiler_succeeds_with_message \
    "${BUILD_DIR}/SysyCC" \
    "${INPUT_FILE}" \
    "semantic warning: function call argument uses incompatible pointer type at 8:27-8:29"

echo "verified: incompatible pointer call arguments warn without failing semantic analysis"
