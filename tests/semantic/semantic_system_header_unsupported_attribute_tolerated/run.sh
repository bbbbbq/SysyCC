#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/semantic_system_header_unsupported_attribute_tolerated.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
SYSTEM_DIR="${SCRIPT_DIR}/system_headers"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
"${BUILD_DIR}/SysyCC" \
    -isystem "${SYSTEM_DIR}" \
    "${INPUT_FILE}" \
    --dump-tokens \
    --dump-parse
assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"

echo "verified: unsupported GNU attributes are tolerated inside system headers"
