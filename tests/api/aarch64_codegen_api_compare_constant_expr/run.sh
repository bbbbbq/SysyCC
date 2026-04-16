#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
CASE_BUILD_DIR="${SCRIPT_DIR}/build"
SOURCE_FILE="${SCRIPT_DIR}/aarch64_codegen_api_compare_constant_expr.cpp"
LL_FILE="${SCRIPT_DIR}/aarch64_codegen_api_compare_constant_expr.ll"
TEST_BIN="${CASE_BUILD_DIR}/aarch64_codegen_api_compare_constant_expr"
OBJECT_FILE="${CASE_BUILD_DIR}/aarch64_codegen_api_compare_constant_expr.o"
EXECUTABLE_FILE="${CASE_BUILD_DIR}/aarch64_codegen_api_compare_constant_expr.bin"

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
"${READELF_TOOL}" -s "${OBJECT_FILE}" | grep -q ' seed$'
"${READELF_TOOL}" -s "${OBJECT_FILE}" | grep -q ' icmp_gt$'
"${READELF_TOOL}" -s "${OBJECT_FILE}" | grep -q ' fcmp_eq$'
"${READELF_TOOL}" -s "${OBJECT_FILE}" | grep -q ' ptr_eq$'
"${READELF_TOOL}" -s "${OBJECT_FILE}" | grep -q ' ptr_ne$'
"${READELF_TOOL}" -s "${OBJECT_FILE}" | grep -q ' vec_cmp$'
"${READELF_TOOL}" -s "${OBJECT_FILE}" | grep -q ' lane_from_cmp_select$'
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

echo "verified: importer folds typed icmp/fcmp constant expressions for scalar, pointer, and vector global initializers"
