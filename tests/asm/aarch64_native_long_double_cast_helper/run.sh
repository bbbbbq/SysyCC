#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
CASE_BUILD_DIR="${SCRIPT_DIR}/build"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
INPUT_FILE="${SCRIPT_DIR}/${TEST_NAME}.sy"
ASM_FILE="${CASE_BUILD_DIR}/${TEST_NAME}.s"
OBJ_FILE="${CASE_BUILD_DIR}/${TEST_NAME}.o"
BIN_FILE="${CASE_BUILD_DIR}/${TEST_NAME}.bin"
TARGET_RUNTIME="${PROJECT_ROOT}/tests/run/support/runtime_target_only.c"

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

grep -q '^  bl __floatsitf$' "${ASM_FILE}"
grep -q '^  bl __trunctfdf2$' "${ASM_FILE}"
grep -q '^  bl __extenddftf2$' "${ASM_FILE}"
grep -q '^  bl __fixtfsi$' "${ASM_FILE}"

object_status=0
build_aarch64_native_object "${ASM_FILE}" "${OBJ_FILE}" || object_status=$?
if [[ "${object_status}" -eq 0 ]]; then
    assert_file_nonempty "${OBJ_FILE}"
    assert_aarch64_relocations "${OBJ_FILE}" '__floatsitf|__trunctfdf2|__extenddftf2|__fixtfsi'
else
    if [[ "${object_status}" -ne 125 ]]; then
        exit "${object_status}"
    fi
    echo "skipped: no AArch64 cross compiler + sysroot for long double cast object smoke"
fi

link_status=0
link_aarch64_native_smoke_binary "${BIN_FILE}" \
    "${OBJ_FILE}" \
    "${TARGET_RUNTIME}" || link_status=$?
if [[ "${link_status}" -eq 0 ]]; then
    assert_file_nonempty "${BIN_FILE}"
    run_status=0
    run_aarch64_native_smoke_if_available "${BIN_FILE}" "5" $'5\n' || run_status=$?
    if [[ "${run_status}" -eq 125 ]]; then
        echo "skipped: no qemu/sysroot for long double cast execute smoke"
    elif [[ "${run_status}" -ne 0 ]]; then
        exit "${run_status}"
    fi
else
    if [[ "${link_status}" -ne 125 ]]; then
        exit "${link_status}"
    fi
    echo "skipped: no AArch64 sysroot/linker for long double cast smoke"
fi

echo "verified: native AArch64 asm lowers long double cast chains through helper calls"
