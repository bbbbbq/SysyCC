#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
TEST_BUILD_DIR="${SCRIPT_DIR}/build"
TEST_BINARY="${TEST_BUILD_DIR}/ir_core_blockaddress_constant"
TEST_SOURCE="${SCRIPT_DIR}/ir_core_blockaddress_constant.cpp"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
mkdir -p "${TEST_BUILD_DIR}"

OBJECT_FILES=()
while IFS= read -r -d '' object_file; do
    OBJECT_FILES+=("${object_file}")
done < <(find "${BUILD_DIR}/CMakeFiles/SysyCC.dir" -name '*.o' ! -name 'main.cpp.o' -print0)

clang++ -std=c++17 -I"${PROJECT_ROOT}/src" \
    "${TEST_SOURCE}" "${OBJECT_FILES[@]}" \
    -L"${BUILD_DIR}" \
    -lsysycc_aarch64_codegen \
    -Wl,-rpath,"${BUILD_DIR}" \
    -o "${TEST_BINARY}"

"${TEST_BINARY}"

echo "verified: Core IR printer, LLVM lowering, and global DCE all preserve blockaddress constants coherently"
