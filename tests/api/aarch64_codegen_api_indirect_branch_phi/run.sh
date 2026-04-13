#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
CASE_BUILD_DIR="${SCRIPT_DIR}/build"
SOURCE_FILE="${SCRIPT_DIR}/aarch64_codegen_api_indirect_branch_phi.cpp"
LL_FILE="${SCRIPT_DIR}/aarch64_codegen_api_indirect_branch_phi.ll"
TEST_BIN="${CASE_BUILD_DIR}/aarch64_codegen_api_indirect_branch_phi"
OBJECT_FILE="${CASE_BUILD_DIR}/aarch64_codegen_api_indirect_branch_phi.o"

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
"${READELF_TOOL}" -s "${OBJECT_FILE}" | grep -q ' dispatch$'
"${READELF_TOOL}" -s "${OBJECT_FILE}" | grep -q ' main$'
"${READELF_TOOL}" -Ws "${OBJECT_FILE}" | grep -F -q '.Ldispatch_entry_to_target_phi'

echo "verified: indirectbr rewrites phi-bearing targets through dedicated phi edge labels in native AArch64 lowering"
