#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
CASE_BUILD_DIR="${SCRIPT_DIR}/build"
INPUT_LL="${SCRIPT_DIR}/cli_riscv64c_public_driver.ll"
INPUT_BC="${CASE_BUILD_DIR}/cli_riscv64c_public_driver.bc"
ASM_FILE="${CASE_BUILD_DIR}/cli_riscv64c_public_driver.s"
OBJ_FILE="${CASE_BUILD_DIR}/cli_riscv64c_public_driver.o"
STANDALONE_BIN="${BUILD_DIR}/sysycc-riscv64c"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
mkdir -p "${CASE_BUILD_DIR}"

assert_file_nonempty "${STANDALONE_BIN}"

PATH="" "${STANDALONE_BIN}" -S --target riscv64-unknown-linux-gnu -o "${ASM_FILE}" \
    "${INPUT_LL}"
assert_file_nonempty "${ASM_FILE}"
grep -Eq '^[[:space:]]*\.globl[[:space:]]+add$' "${ASM_FILE}"
grep -Eq '^add:$' "${ASM_FILE}"

cp "${PROJECT_ROOT}/tests/api/aarch64_codegen_api_smoke/aarch64_codegen_api_smoke.bc" \
   "${INPUT_BC}"
PATH="" "${STANDALONE_BIN}" -c --target riscv64-unknown-linux-gnu -o "${OBJ_FILE}" \
    "${INPUT_BC}"
assert_file_nonempty "${OBJ_FILE}"
READELF_TOOL="$(find_aarch64_readelf)"
"${READELF_TOOL}" -h "${OBJ_FILE}" | grep -Eq 'Machine:.*RISC-V'
"${READELF_TOOL}" -s "${OBJ_FILE}" | grep -Eq ' add$'

echo "verified: standalone sysycc-riscv64c CLI compiles .ll and .bc without compiler involvement"
