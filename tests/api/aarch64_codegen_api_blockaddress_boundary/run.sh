#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
CASE_BUILD_DIR="${SCRIPT_DIR}/build"
SOURCE_FILE="${SCRIPT_DIR}/aarch64_codegen_api_blockaddress_boundary.cpp"
LL_FILE="${SCRIPT_DIR}/aarch64_codegen_api_blockaddress_boundary.ll"
TEST_BIN="${CASE_BUILD_DIR}/aarch64_codegen_api_blockaddress_boundary"
OBJECT_FILE="${CASE_BUILD_DIR}/aarch64_codegen_api_blockaddress_boundary.o"

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
"${READELF_TOOL}" -s "${OBJECT_FILE}" | grep -q ' jump_target$'
"${READELF_TOOL}" -s "${OBJECT_FILE}" | grep -q ' jump_target_i64$'
"${READELF_TOOL}" -s "${OBJECT_FILE}" | grep -q ' get_target_i64$'
"${READELF_TOOL}" -s "${OBJECT_FILE}" | grep -q ' foo$'
"${READELF_TOOL}" -s "${OBJECT_FILE}" | grep -F -q '.Lfoo_target'
"${READELF_TOOL}" -r "${OBJECT_FILE}" | grep -F -q '.Lfoo_target'

echo "verified: importer lowers blockaddress constants through native AArch64 asm/object local text-label symbols"
