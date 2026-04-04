#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_float_literal_suffix.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
IR_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.ll"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ir

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"
assert_file_nonempty "${IR_FILE}"

grep -Eq 'define float @take_float\(\)' "${IR_FILE}"
grep -Eq 'ret float (0\.5|5\.0+e-01)' "${IR_FILE}"
if grep -Eq 'fptrunc double .* to float' "${IR_FILE}"; then
    echo "unexpected double-to-float truncation for 0.5f" >&2
    exit 1
fi

grep -Eq 'define fp128 @take_long_double\(\)' "${IR_FILE}"
grep -Eq 'fpext double (0\.5|5\.0+e-01) to fp128' "${IR_FILE}"
grep -Eq 'ret fp128 %t[0-9]+' "${IR_FILE}"

grep -Eq 'define double @take_double\(\)' "${IR_FILE}"
grep -Eq 'ret double (0\.5|5\.0+e-01)' "${IR_FILE}"

echo "verified: ir preserves float, double, and long-double literal suffix types"
