#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_core_inliner_loopified_scalar_call.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
CORE_IR_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.core-ir.txt"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" -include "${PROJECT_ROOT}/tests/compiler2025/sylib.h" \
    "${INPUT_FILE}" --dump-core-ir >/dev/null

assert_file_nonempty "${CORE_IR_FILE}"

grep -Eq '^func @loopified_scalar\(i32 %n, i32 %depth\) -> i32 \{$' "${CORE_IR_FILE}"

MAIN_CORE_IR="$(sed -n '/^func @main()/,/^}/p' "${CORE_IR_FILE}")"
grep -Eq 'call i32 @loopified_scalar' <<<"${MAIN_CORE_IR}"

if grep -Eq 'loopified_scalar\.inline' <<<"${MAIN_CORE_IR}"; then
    echo "expected loopified scalar helper to stay out of main" >&2
    exit 1
fi

echo "verified: inliner keeps loopified scalar helper as a direct call"
