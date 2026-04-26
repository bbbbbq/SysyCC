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

WRAP_BODY="$(awk '
    $0 == "wrap_call:" { capture = 1; next }
    capture && $0 == ".size wrap_call, .-wrap_call" { exit }
    capture { print }
' "${ASM_FILE}")"

printf '%s\n' "${WRAP_BODY}" | grep -q '^  stp x29, x30, \[sp, #-16\]!$'
printf '%s\n' "${WRAP_BODY}" | grep -q '^  mov x29, sp$'
printf '%s\n' "${WRAP_BODY}" | grep -q '^  bl leaf_add1$'
printf '%s\n' "${WRAP_BODY}" | grep -q '^  ldp x29, x30, \[sp\], #16$'
if printf '%s\n' "${WRAP_BODY}" | grep -Eq '^  sub sp, sp, #'; then
    echo "unexpected extra local-frame allocation in zero-frame non-leaf shell" >&2
    exit 1
fi

echo "verified: zero-frame non-leaf functions still preserve lr in the native AArch64 shell"
