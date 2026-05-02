#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
CASE_BUILD_DIR="${SCRIPT_DIR}/build"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
MAIN_SOURCE="${SCRIPT_DIR}/run_aarch64_multi_object_link_smoke_main.c"
HELPER_SOURCE="${SCRIPT_DIR}/run_aarch64_multi_object_link_smoke_helper.c"
MAIN_LL="${CASE_BUILD_DIR}/${TEST_NAME}.main.ll"
HELPER_LL="${CASE_BUILD_DIR}/${TEST_NAME}.helper.ll"
MAIN_OBJECT="${CASE_BUILD_DIR}/${TEST_NAME}.main.o"
HELPER_OBJECT="${CASE_BUILD_DIR}/${TEST_NAME}.helper.o"
PROGRAM_FILE="${CASE_BUILD_DIR}/${TEST_NAME}.bin"
RELOCATION_FILE="${CASE_BUILD_DIR}/${TEST_NAME}.main.relocations.txt"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

HOST_CLANG="${SYSYCC_HOST_CLANG:-clang}"
AARCH64_CC="$(find_aarch64_cc)"
SYSROOT="$(find_aarch64_sysroot)"
READELF="$(command -v aarch64-linux-gnu-readelf || command -v llvm-readelf || command -v readelf || true)"

mkdir -p "${CASE_BUILD_DIR}"

"${HOST_CLANG}" \
    --target=aarch64-unknown-linux-gnu \
    --sysroot="${SYSROOT}" \
    -std=gnu11 \
    -S -emit-llvm -O0 \
    -Xclang -disable-O0-optnone \
    -fno-stack-protector \
    -fno-unwind-tables \
    -fno-asynchronous-unwind-tables \
    -fno-builtin \
    "${MAIN_SOURCE}" \
    -o "${MAIN_LL}"
"${HOST_CLANG}" \
    --target=aarch64-unknown-linux-gnu \
    --sysroot="${SYSROOT}" \
    -std=gnu11 \
    -S -emit-llvm -O0 \
    -Xclang -disable-O0-optnone \
    -fno-stack-protector \
    -fno-unwind-tables \
    -fno-asynchronous-unwind-tables \
    -fno-builtin \
    "${HELPER_SOURCE}" \
    -o "${HELPER_LL}"

assert_file_nonempty "${MAIN_LL}"
assert_file_nonempty "${HELPER_LL}"

"${BUILD_DIR}/sysycc-aarch64c" -c -fPIC "${MAIN_LL}" -o "${MAIN_OBJECT}"
"${BUILD_DIR}/sysycc-aarch64c" -c -fPIC "${HELPER_LL}" -o "${HELPER_OBJECT}"

assert_file_nonempty "${MAIN_OBJECT}"
assert_file_nonempty "${HELPER_OBJECT}"

if [[ -n "${READELF}" ]]; then
    "${READELF}" -r "${MAIN_OBJECT}" >"${RELOCATION_FILE}"
    grep -q 'helper' "${RELOCATION_FILE}"
    grep -q 'shared_counter' "${RELOCATION_FILE}"
    grep -q 'shared_counter_slot' "${RELOCATION_FILE}"
fi

run_aarch64_cc "${AARCH64_CC}" "${MAIN_OBJECT}" "${HELPER_OBJECT}" -o "${PROGRAM_FILE}"
assert_file_nonempty "${PROGRAM_FILE}"

if ! have_aarch64_binary_runtime; then
    echo "skipped runtime: missing AArch64 qemu/docker runner"
    exit 0
fi

PROGRAM_OUTPUT="$(run_aarch64_binary_with_available_runtime "${PROGRAM_FILE}" "${SYSROOT}")"
if [[ -n "${PROGRAM_OUTPUT}" ]]; then
    echo "unexpected AArch64 multi-object smoke output: '${PROGRAM_OUTPUT}'" >&2
    exit 1
fi

echo "verified: direct AArch64 multi-object mixed code/data PIC smoke emits .o files, links, and runs"
