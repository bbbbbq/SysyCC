#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
CASE_BUILD_DIR="${SCRIPT_DIR}/build"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
INPUT_FILE="${SCRIPT_DIR}/${TEST_NAME}.sy"
OBJ_FILE="${CASE_BUILD_DIR}/${TEST_NAME}.o"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
mkdir -p "${CASE_BUILD_DIR}"

"${BUILD_DIR}/SysyCC" \
    --dump-tokens \
    --dump-parse \
    -c \
    --backend=aarch64-native \
    --target=aarch64-unknown-linux-gnu \
    -o "${OBJ_FILE}" \
    "${INPUT_FILE}"

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"
assert_file_nonempty "${OBJ_FILE}"

OBJDUMP_TOOL="$(command -v aarch64-linux-gnu-objdump 2>/dev/null || command -v llvm-objdump 2>/dev/null || true)"
if [[ -z "${OBJDUMP_TOOL}" ]]; then
    echo "skipped: no target-capable objdump available for float/unsigned object inspection"
    exit 0
fi

DUMP_OUTPUT="$("${OBJDUMP_TOOL}" -dr -M no-aliases "${OBJ_FILE}")"
grep -Eq 'ucvtf[[:space:]]+s[0-9]+, w[0-9]+' <<<"${DUMP_OUTPUT}"
grep -Eq 'fcvtzu[[:space:]]+w[0-9]+, s[0-9]+' <<<"${DUMP_OUTPUT}"

echo "verified: native AArch64 object emission now covers float/unsigned conversion opcodes directly"
