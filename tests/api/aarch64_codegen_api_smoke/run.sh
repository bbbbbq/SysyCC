#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
CASE_BUILD_DIR="${SCRIPT_DIR}/build"
SOURCE_FILE="${SCRIPT_DIR}/aarch64_codegen_api_smoke.cpp"
LL_FILE="${SCRIPT_DIR}/aarch64_codegen_api_smoke.ll"
BC_FILE="${SCRIPT_DIR}/aarch64_codegen_api_smoke.bc"
SMOKE_BIN="${CASE_BUILD_DIR}/aarch64_codegen_api_smoke"
OBJECT_FILE="${CASE_BUILD_DIR}/aarch64_codegen_api_smoke.o"
EXECUTABLE_FILE="${CASE_BUILD_DIR}/aarch64_codegen_api_smoke.bin"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
mkdir -p "${CASE_BUILD_DIR}"

"${CXX:-c++}" \
    -std=c++17 \
    -I"${PROJECT_ROOT}/src" \
    "${SOURCE_FILE}" \
    -L"${BUILD_DIR}" \
    -lsysycc_aarch64_codegen \
    -Wl,-rpath,"${BUILD_DIR}" \
    -o "${SMOKE_BIN}"

assert_file_nonempty "${SMOKE_BIN}"
assert_file_nonempty "${BC_FILE}"
PATH="" "${SMOKE_BIN}" "${LL_FILE}" "${BC_FILE}" "${OBJECT_FILE}" >/dev/null
assert_file_nonempty "${OBJECT_FILE}"
READELF_TOOL="$(find_aarch64_readelf)"
"${READELF_TOOL}" -S "${OBJECT_FILE}" | grep -q '\.text'
"${READELF_TOOL}" -s "${OBJECT_FILE}" | grep -q ' add$'
"${READELF_TOOL}" -s "${OBJECT_FILE}" | grep -q ' values$'

CC_TOOL="$(find_aarch64_cc 2>/dev/null || true)"
SYSROOT="$(find_aarch64_sysroot 2>/dev/null || true)"
QEMU_TOOL="$(find_qemu_aarch64 2>/dev/null || true)"
if [[ -n "${CC_TOOL}" ]] && [[ -n "${SYSROOT}" ]] && [[ -n "${QEMU_TOOL}" ]]; then
    run_aarch64_cc "${CC_TOOL}" "${OBJECT_FILE}" -o "${EXECUTABLE_FILE}"
    set +e
    QEMU_LD_PREFIX="${SYSROOT}" "${QEMU_TOOL}" -L "${SYSROOT}" "${EXECUTABLE_FILE}" >/dev/null 2>&1
    EXIT_CODE=$?
    set -e
    [[ "${EXIT_CODE}" -eq 11 ]]
fi

echo "verified: external callers can link libsysycc_aarch64_codegen and compile both textual LLVM IR and LLVM bitcode to native AArch64 asm/object output"
