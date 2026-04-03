#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_unsigned_right_shift.sy"
IR_FILE="${BUILD_DIR}/intermediate_results/ir_unsigned_right_shift.ll"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ir

assert_file_nonempty "${IR_FILE}"
grep -q 'lshr i32' "${IR_FILE}"
if rg -q 'ashr i32' "${IR_FILE}"; then
    echo "unexpected arithmetic right shift for unsigned int" >&2
    exit 1
fi

echo "verified: ir lowers unsigned right shift with logical shift"
