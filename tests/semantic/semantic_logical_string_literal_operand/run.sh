#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${SYSYCC_BUILD_DIR:-${PROJECT_ROOT}/build}"
INPUT_FILE="${SCRIPT_DIR}/semantic_logical_string_literal_operand.sy"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
"${BUILD_DIR}/SysyCC" --stop-after=semantic "${INPUT_FILE}" >/dev/null

echo "verified: logical operators accept decayed string literal operands"
