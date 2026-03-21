#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_ptrdiff_pointer_difference.sy"
IR_FILE="${BUILD_DIR}/intermediate_results/ir_ptrdiff_pointer_difference.ll"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ir

assert_file_nonempty "${IR_FILE}"
grep -q '^define i64 @distance' "${IR_FILE}"
grep -q 'sdiv i64 ' "${IR_FILE}"
grep -q 'ret i64 ' "${IR_FILE}"
if grep -q 'trunc i64 .* to i32' "${IR_FILE}"; then
    echo "unexpected i64-to-i32 truncation in ptrdiff_t pointer difference" >&2
    exit 1
fi

echo "verified: pointer difference preserves ptrdiff-width IR results"
