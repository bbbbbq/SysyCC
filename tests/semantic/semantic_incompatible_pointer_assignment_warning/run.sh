#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/semantic_incompatible_pointer_assignment_warning.sy"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
assert_compiler_succeeds_with_message \
    "${BUILD_DIR}/SysyCC" \
    "${INPUT_FILE}" \
    "semantic warning: assignment between incompatible pointer types at 5:5-5:19"

echo "verified: incompatible pointer assignments warn without failing semantic analysis"
