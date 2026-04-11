#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
CASE_BUILD_DIR="${SCRIPT_DIR}/build"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
INPUT_FILE="${SCRIPT_DIR}/${TEST_NAME}.sy"
OBJ_FILE="${CASE_BUILD_DIR}/${TEST_NAME}.o"
BIN_FILE="${CASE_BUILD_DIR}/${TEST_NAME}.bin"
RUNTIME_SOURCE="${PROJECT_ROOT}/tests/run/support/runtime_target_only.c"

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
if [[ -n "${OBJDUMP_TOOL}" ]]; then
    DUMP_OUTPUT="$("${OBJDUMP_TOOL}" -dr -M no-aliases "${OBJ_FILE}")"
    grep -Eq 'blr[[:space:]]+x[0-9]+' <<<"${DUMP_OUTPUT}"
    grep -Eq 'orr[[:space:]]+w[0-9]+, wzr, w0' <<<"${DUMP_OUTPUT}"
fi

link_status=0
link_aarch64_native_smoke_binary "${OBJ_FILE}" "${RUNTIME_SOURCE}" "${BIN_FILE}" || link_status=$?
if [[ "${link_status}" -eq 125 ]]; then
    echo "skipped: no AArch64 linker/sysroot for indirect-call object smoke"
    exit 0
fi

assert_file_nonempty "${BIN_FILE}"

run_status=0
run_aarch64_native_smoke_if_available "${BIN_FILE}" "11" || run_status=$?
if [[ "${run_status}" -eq 125 ]]; then
    echo "skipped: no qemu/sysroot for indirect-call object execute smoke"
    exit 0
fi

echo "verified: native AArch64 object emission now preserves indirect-call return values through direct object writing"
