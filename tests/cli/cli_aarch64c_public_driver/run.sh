#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
CASE_BUILD_DIR="${SCRIPT_DIR}/build"
INPUT_LL="${SCRIPT_DIR}/cli_aarch64c_public_driver.ll"
INPUT_BC="${CASE_BUILD_DIR}/cli_aarch64c_public_driver.bc"
ASM_FILE="${CASE_BUILD_DIR}/cli_aarch64c_public_driver.s"
OBJ_FILE="${CASE_BUILD_DIR}/cli_aarch64c_public_driver.o"
STANDALONE_BIN="${BUILD_DIR}/sysycc-aarch64c"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
mkdir -p "${CASE_BUILD_DIR}"

assert_file_nonempty "${STANDALONE_BIN}"

"${STANDALONE_BIN}" -S --target aarch64-unknown-linux-gnu -o "${ASM_FILE}" \
    "${INPUT_LL}"
assert_file_nonempty "${ASM_FILE}"
grep -q '^\.globl add$' "${ASM_FILE}"
grep -q '^  bl add$' "${ASM_FILE}"

cp "${PROJECT_ROOT}/tests/api/aarch64_codegen_api_smoke/aarch64_codegen_api_smoke.bc" \
   "${INPUT_BC}"
PATH="" "${STANDALONE_BIN}" -c --target aarch64-unknown-linux-gnu -o "${OBJ_FILE}" \
    "${INPUT_BC}"
assert_file_nonempty "${OBJ_FILE}"
READELF_TOOL="$(find_aarch64_readelf)"
"${READELF_TOOL}" -S "${OBJ_FILE}" | grep -q '\.text'
"${READELF_TOOL}" -s "${OBJ_FILE}" | grep -q ' add$'

echo "verified: standalone sysycc-aarch64c CLI compiles .ll and .bc without compiler involvement"
