#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
CASE_BUILD_DIR="${SCRIPT_DIR}/build"
SOURCE_FILE="${SCRIPT_DIR}/riscv64_codegen_api_smoke.cpp"
LL_FILE="${SCRIPT_DIR}/riscv64_codegen_api_smoke.ll"
BC_FILE="${CASE_BUILD_DIR}/riscv64_codegen_api_smoke.bc"
SMOKE_BIN="${CASE_BUILD_DIR}/riscv64_codegen_api_smoke"
OBJECT_FILE="${CASE_BUILD_DIR}/riscv64_codegen_api_smoke.o"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
mkdir -p "${CASE_BUILD_DIR}"

"${CXX:-c++}" \
    -std=c++17 \
    -I"${PROJECT_ROOT}/src" \
    "${SOURCE_FILE}" \
    -L"${BUILD_DIR}" \
    -lsysycc_riscv64_codegen \
    -Wl,-rpath,"${BUILD_DIR}" \
    -o "${SMOKE_BIN}"

assert_file_nonempty "${SMOKE_BIN}"
cp "${PROJECT_ROOT}/tests/api/aarch64_codegen_api_smoke/aarch64_codegen_api_smoke.bc" \
   "${BC_FILE}"
PATH="" "${SMOKE_BIN}" "${LL_FILE}" "${BC_FILE}" "${OBJECT_FILE}" >/dev/null
assert_file_nonempty "${OBJECT_FILE}"
READELF_TOOL="$(find_aarch64_readelf)"
"${READELF_TOOL}" -h "${OBJECT_FILE}" | grep -Eq 'Machine:.*RISC-V'
"${READELF_TOOL}" -s "${OBJECT_FILE}" | grep -Eq ' add$'
"${READELF_TOOL}" -s "${OBJECT_FILE}" | grep -Eq ' values$'

echo "verified: external callers can link libsysycc_riscv64_codegen and compile textual LLVM IR plus LLVM bitcode to native RISC-V64 asm/object output"
