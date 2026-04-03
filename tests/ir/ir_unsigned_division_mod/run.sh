#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_unsigned_division_mod.sy"
IR_FILE="${BUILD_DIR}/intermediate_results/ir_unsigned_division_mod.ll"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ir

assert_file_nonempty "${IR_FILE}"
grep -q 'udiv i32' "${IR_FILE}"
grep -q 'urem i32' "${IR_FILE}"
if rg -q 'sdiv i32|srem i32' "${IR_FILE}"; then
    echo "unexpected signed div/rem for unsigned int" >&2
    exit 1
fi

echo "verified: ir lowers unsigned division and modulo with unsigned ops"
