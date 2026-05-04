#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_core_inliner_loopified_pointer_hot_loop.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
CORE_IR_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.core-ir.txt"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-core-ir >/dev/null

assert_file_nonempty "${CORE_IR_FILE}"

MAIN_CORE_IR="$(sed -n '/^func @main()/,/^}/p' "${CORE_IR_FILE}")"

if grep -Eq 'call void @step' <<<"${MAIN_CORE_IR}"; then
    echo "expected hot-loop pointer helper to inline into main" >&2
    exit 1
fi

grep -Eq 'step\.inline' <<<"${MAIN_CORE_IR}"

echo "verified: inliner pulls loopified pointer helpers into hot caller loops"
