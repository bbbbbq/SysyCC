#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
CASE_BUILD_DIR="${SCRIPT_DIR}/build"
SOURCE_FILE="${SCRIPT_DIR}/aarch64_codegen_api_scalar_constant_cast.cpp"
LL_FILE="${SCRIPT_DIR}/aarch64_codegen_api_scalar_constant_cast.ll"
TEST_BIN="${CASE_BUILD_DIR}/aarch64_codegen_api_scalar_constant_cast"
OBJECT_FILE="${CASE_BUILD_DIR}/aarch64_codegen_api_scalar_constant_cast.o"
EXECUTABLE_FILE="${CASE_BUILD_DIR}/aarch64_codegen_api_scalar_constant_cast.bin"

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
    -o "${TEST_BIN}"

assert_file_nonempty "${TEST_BIN}"
"${TEST_BIN}" "${LL_FILE}" "${OBJECT_FILE}"
assert_file_nonempty "${OBJECT_FILE}"
READELF_TOOL="$(find_aarch64_readelf)"
"${READELF_TOOL}" -s "${OBJECT_FILE}" | grep -q ' trunc_i32$'
"${READELF_TOOL}" -s "${OBJECT_FILE}" | grep -q ' zext_i64$'
"${READELF_TOOL}" -s "${OBJECT_FILE}" | grep -q ' sext_i64$'
"${READELF_TOOL}" -s "${OBJECT_FILE}" | grep -q ' flt32$'
"${READELF_TOOL}" -s "${OBJECT_FILE}" | grep -q ' dbl_to_i32$'
"${READELF_TOOL}" -s "${OBJECT_FILE}" | grep -q ' bit_bits$'
"${READELF_TOOL}" -s "${OBJECT_FILE}" | grep -q ' main$'

CC_TOOL="$(find_aarch64_cc 2>/dev/null || true)"
SYSROOT="$(find_aarch64_sysroot 2>/dev/null || true)"
QEMU_TOOL="$(find_qemu_aarch64 2>/dev/null || true)"
if [[ -n "${CC_TOOL}" ]] && [[ -n "${SYSROOT}" ]] && [[ -n "${QEMU_TOOL}" ]]; then
    run_aarch64_cc "${CC_TOOL}" "${OBJECT_FILE}" -o "${EXECUTABLE_FILE}"
    set +e
    QEMU_LD_PREFIX="${SYSROOT}" "${QEMU_TOOL}" -L "${SYSROOT}" "${EXECUTABLE_FILE}" >/dev/null 2>&1
    EXIT_CODE=$?
    set -e
    [[ "${EXIT_CODE}" -eq 0 ]]
fi

echo "verified: importer folds typed scalar LLVM constant casts into native-friendly constants for asm/object emission"
