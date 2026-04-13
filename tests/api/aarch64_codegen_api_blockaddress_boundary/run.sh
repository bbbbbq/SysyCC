#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
CASE_BUILD_DIR="${SCRIPT_DIR}/build"
SOURCE_FILE="${SCRIPT_DIR}/aarch64_codegen_api_blockaddress_boundary.cpp"
LL_FILE="${SCRIPT_DIR}/aarch64_codegen_api_blockaddress_boundary.ll"
TEST_BIN="${CASE_BUILD_DIR}/aarch64_codegen_api_blockaddress_boundary"

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
"${TEST_BIN}" "${LL_FILE}"

echo "verified: importer recognizes blockaddress constants and reports a precise unsupported-boundary diagnostic"
