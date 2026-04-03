#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/semantic_relational_pointer.sy"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
assert_compiler_fails_with_message \
    "${BUILD_DIR}/SysyCC" --stop-after=semantic \
    "${INPUT_FILE}" \
    "semantic error: binary operator '<' requires arithmetic operands"

echo "verified: semantic analysis checks relational pointer operands"
