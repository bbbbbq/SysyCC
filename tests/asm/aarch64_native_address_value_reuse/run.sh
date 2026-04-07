#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
CASE_BUILD_DIR="${SCRIPT_DIR}/build"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
INPUT_FILE="${SCRIPT_DIR}/${TEST_NAME}.sy"
ASM_FILE="${CASE_BUILD_DIR}/${TEST_NAME}.s"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
mkdir -p "${CASE_BUILD_DIR}"

"${BUILD_DIR}/SysyCC" \
    --dump-tokens \
    --dump-parse \
    -S \
    --backend=aarch64-native \
    --target=aarch64-unknown-linux-gnu \
    -o "${ASM_FILE}" \
    "${INPUT_FILE}"

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"
assert_file_nonempty "${ASM_FILE}"

assert_no_illegal_aarch64_index_forms "${ASM_FILE}"
test "$(grep -Ec '^[[:space:]]*sxtw x[0-9]+, w[0-9]+$' "${ASM_FILE}")" -eq 1
test "$(grep -Ec '^[[:space:]]*add x[0-9]+, x[0-9]+, x[0-9]+, lsl #2$' "${ASM_FILE}")" -eq 1
test "$(grep -Ec '^[[:space:]]*ldr w[0-9]+, \[x[0-9]+(, #0)?\]$' "${ASM_FILE}")" -eq 2
test "$(grep -Ec '^[[:space:]]*stur x[0-9]+, \[x29, #-[0-9]+\]$' "${ASM_FILE}")" -eq 0
test "$(grep -Ec '^[[:space:]]*ldur x[0-9]+, \[x29, #-[0-9]+\]$' "${ASM_FILE}")" -eq 0

echo "verified: native AArch64 asm reuses one computed address value instead of rematerializing or parking it in frame memory"
