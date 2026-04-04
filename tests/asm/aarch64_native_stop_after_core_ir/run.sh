#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
CASE_BUILD_DIR="${SCRIPT_DIR}/build"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
INPUT_FILE="${SCRIPT_DIR}/${TEST_NAME}.sy"
ASM_FILE="${CASE_BUILD_DIR}/${TEST_NAME}.s"
CORE_IR_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.core-ir.txt"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
mkdir -p "${CASE_BUILD_DIR}"
rm -f "${ASM_FILE}"

"${BUILD_DIR}/SysyCC" \
    --dump-tokens \
    --dump-parse \
    --dump-core-ir \
    -S \
    --backend=aarch64-native \
    --target=aarch64-unknown-linux-gnu \
    --stop-after=core-ir \
    -o "${ASM_FILE}" \
    "${INPUT_FILE}"

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"
assert_file_nonempty "${CORE_IR_FILE}"

if [[ -e "${ASM_FILE}" ]]; then
    echo "error: asm file should not be emitted when stopping after core-ir" >&2
    exit 1
fi

grep -q '^func @main' "${CORE_IR_FILE}"

echo "verified: stop-after=core-ir dumps optimized core ir and skips asm emission"
