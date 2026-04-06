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
test "$(grep -Ec '^[[:space:]]*add x[0-9]+, x[0-9]+, x[0-9]+, lsl #2$' "${ASM_FILE}")" -ge 2
grep -q '^\.globl read_at$' "${ASM_FILE}"
grep -q '^\.globl read_cast$' "${ASM_FILE}"
if grep -Eq '^[[:space:]]*sxtw x[0-9]+, w[0-9]+$' "${ASM_FILE}"; then
    echo "unexpected sign extension for unsigned index legalization" >&2
    exit 1
fi

echo "verified: direct and cast-to-unsigned 32-bit indices zero-extend before native AArch64 address arithmetic"
