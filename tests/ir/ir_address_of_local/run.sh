#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_address_of_local.sy"
IR_FILE="${BUILD_DIR}/intermediate_results/ir_address_of_local.ll"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ir

assert_file_nonempty "${IR_FILE}"
if ! grep -Eq 'store ptr %value\.addr|%value\.addr = alloca i32' "${IR_FILE}"; then
    echo "expected address-of-local lowering either to materialize a pointer store or to keep the local slot directly" >&2
    exit 1
fi

echo "verified: ir lowers address-of on local variables"
