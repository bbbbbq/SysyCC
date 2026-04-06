#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_initializer_integer_conversion.sy"
IR_FILE="${BUILD_DIR}/intermediate_results/ir_initializer_integer_conversion.ll"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ir

assert_file_nonempty "${IR_FILE}"
if ! grep -q 'sext i32 .* to i64' "${IR_FILE}"; then
    grep -q '^  %t0 = call i32 @getint()$' "${IR_FILE}"
    grep -q '^  ret i32 %t0$' "${IR_FILE}"
fi

echo "verified: initializer integer conversions either lower explicitly or fold away after optimization"
