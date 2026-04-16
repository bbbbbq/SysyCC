#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
CASE_BUILD_DIR="${SCRIPT_DIR}/build"
INPUT_FILE="${SCRIPT_DIR}/cli_riscv64_native_llvm_ir_artifacts.sy"
OBJECT_FILE="${CASE_BUILD_DIR}/cli_riscv64_native_llvm_ir_artifacts.o"
ARTIFACT_DIR="${BUILD_DIR}/intermediate_results"
LL_FILE="${ARTIFACT_DIR}/cli_riscv64_native_llvm_ir_artifacts.ll"
BC_FILE="${ARTIFACT_DIR}/cli_riscv64_native_llvm_ir_artifacts.bc"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
mkdir -p "${CASE_BUILD_DIR}"
rm -f "${LL_FILE}" "${BC_FILE}" "${OBJECT_FILE}"
SYSYCC_BIN="$(get_real_sysycc_binary_path "${BUILD_DIR}")"

PATH="" \
    "${SYSYCC_BIN}" \
    -c \
    --backend=riscv64-native \
    --target=riscv64-unknown-linux-gnu \
    -o "${OBJECT_FILE}" \
    "${INPUT_FILE}"

assert_file_nonempty "${OBJECT_FILE}"
assert_file_nonempty "${LL_FILE}"
assert_file_nonempty "${BC_FILE}"
READELF_TOOL="$(find_aarch64_readelf)"
"${READELF_TOOL}" -h "${OBJECT_FILE}" | grep -Eq 'Machine:.*RISC-V'
"${READELF_TOOL}" -s "${OBJECT_FILE}" | grep -Eq ' add$'

echo "verified: native compiler path materializes stable LLVM IR text/bitcode artifacts before invoking the decoupled RISC-V64 codegen library"
