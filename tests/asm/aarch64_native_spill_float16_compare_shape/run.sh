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

grep -q '^\.arch armv8.2-a+fp16$' "${ASM_FILE}"
test "$(grep -Ec '^[[:space:]]*fadd s[0-9]+, s[0-9]+, s[0-9]+$' "${ASM_FILE}")" -ge 2
test "$(grep -Ec '^[[:space:]]*fcmp s[0-9]+, s[0-9]+$' "${ASM_FILE}")" -ge 1
test "$(grep -Ec '^[[:space:]]*cset w[0-9]+, lt$' "${ASM_FILE}")" -ge 1
if grep -Eq '%[ud][0-9]+[wx]' "${ASM_FILE}"; then
    echo "unexpected virtual register token leaked into final asm" >&2
    exit 1
fi

echo "verified: spill-heavy _Float16 compare lowering still reaches promoted fcmp/cset without leaking virtual operands"
