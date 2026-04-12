#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
CASE_BUILD_DIR="${SCRIPT_DIR}/build"
SOURCE_FILE="${SCRIPT_DIR}/aarch64_codegen_api_vector_syntax.cpp"
LL_FILE="${SCRIPT_DIR}/aarch64_codegen_api_vector_syntax.ll"
TEST_BIN="${CASE_BUILD_DIR}/aarch64_codegen_api_vector_syntax"
OBJECT_FILE="${CASE_BUILD_DIR}/aarch64_codegen_api_vector_syntax.o"

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
"${READELF_TOOL}" -S "${OBJECT_FILE}" | grep -q '\.text'
"${READELF_TOOL}" -s "${OBJECT_FILE}" | grep -q ' vec_demo$'

echo "verified: Phase 3 importer scalarizes a basic textual LLVM vector slice into native AArch64 asm/object output"
