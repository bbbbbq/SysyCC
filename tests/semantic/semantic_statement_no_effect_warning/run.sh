#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/semantic_statement_no_effect_warning.sy"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
assert_compiler_succeeds_with_message \
    "${BUILD_DIR}/SysyCC" -Wall --stop-after=semantic \
    "${INPUT_FILE}" \
    "semantic warning: statement has no effect"

echo "verified: semantic analysis warns about side-effect-free expression statements"
