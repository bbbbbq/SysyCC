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
    -fPIC \
    --backend=aarch64-native \
    --target=aarch64-unknown-linux-gnu \
    -o "${ASM_FILE}" \
    "${INPUT_FILE}"

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"
assert_file_nonempty "${ASM_FILE}"

grep -Eq '^[[:space:]]*adrp x[0-9]+, :got:ext$' "${ASM_FILE}"
grep -Eq '^[[:space:]]*ldr x[0-9]+, \[x[0-9]+, :got_lo12:ext\]$' "${ASM_FILE}"
if grep -Eq '%[ud][0-9]+[wx]' "${ASM_FILE}"; then
    echo "unexpected virtual register token leaked into final asm" >&2
    exit 1
fi

echo "verified: PIC native AArch64 asm prints typed symbolic memory offsets for GOT loads"
