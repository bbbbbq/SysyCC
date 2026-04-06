#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/defined_malformed.sy"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

assert_compiler_fails_with_message \
    "${BUILD_DIR}/SysyCC" \
    -E \
    "${INPUT_FILE}" \
    "invalid defined() operand in #if expression"

echo "verified: malformed defined() expressions are rejected explicitly"
