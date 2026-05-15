#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
TEST_BUILD_DIR="${SCRIPT_DIR}/build"
TEST_BINARY="${TEST_BUILD_DIR}/ir_core_const_fold_pass"
TEST_SOURCE="${SCRIPT_DIR}/ir_core_const_fold_pass.cpp"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

mkdir -p "${TEST_BUILD_DIR}"

STATIC_LIBS=()
while IFS= read -r -d '' lib; do
    STATIC_LIBS+=("${lib}")
done < <(find "${BUILD_DIR}" -name 'libsysycc_*.a' -print0)

SHARED_LIBS=()
while IFS= read -r -d '' lib; do
    SHARED_LIBS+=("${lib}")
done < <(find "${BUILD_DIR}" -name 'libsysycc_*.so' -print0)

clang++ -std=c++17 -I"${PROJECT_ROOT}/src" \
    "${TEST_SOURCE}" \
    "${STATIC_LIBS[@]}" \
    -L"${BUILD_DIR}" \
    -lsysycc_aarch64_codegen -lsysycc_riscv64_codegen \
    -Wl,-rpath,"${BUILD_DIR}" \
    -lffi \
    -o "${TEST_BINARY}"

"${TEST_BINARY}"

echo "verified: CoreIrConstFoldPass folds local integer constants"
