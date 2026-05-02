#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
CASE_BUILD_DIR="${SCRIPT_DIR}/build"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
MAIN_SOURCE="${SCRIPT_DIR}/run_aarch64_multi_object_func_call_main.c"
HELPER_SOURCE="${SCRIPT_DIR}/run_aarch64_multi_object_func_call_helper.c"
MAIN_LL="${CASE_BUILD_DIR}/${TEST_NAME}.main.ll"
HELPER_LL="${CASE_BUILD_DIR}/${TEST_NAME}.helper.ll"
MAIN_OBJECT="${CASE_BUILD_DIR}/${TEST_NAME}.main.o"
HELPER_OBJECT="${CASE_BUILD_DIR}/${TEST_NAME}.helper.o"
PROGRAM_FILE="${CASE_BUILD_DIR}/${TEST_NAME}.bin"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

HOST_CLANG="${SYSYCC_HOST_CLANG:-clang}"
AARCH64_CC="$(find_aarch64_cc)"
SYSROOT="$(find_aarch64_sysroot)"

mkdir -p "${CASE_BUILD_DIR}"

for pair in \
    "${MAIN_SOURCE}:${MAIN_LL}:${MAIN_OBJECT}" \
    "${HELPER_SOURCE}:${HELPER_LL}:${HELPER_OBJECT}"; do
    IFS=":" read -r source_file ll_file object_file <<<"${pair}"
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
        "${source_file}" \
        -o "${ll_file}"
    assert_file_nonempty "${ll_file}"
    "${BUILD_DIR}/sysycc-aarch64c" -c -fPIC "${ll_file}" -o "${object_file}"
    assert_file_nonempty "${object_file}"
done

assert_aarch64_relocations "${MAIN_OBJECT}" 'helper'

run_aarch64_cc "${AARCH64_CC}" "${MAIN_OBJECT}" "${HELPER_OBJECT}" -o "${PROGRAM_FILE}"
assert_file_nonempty "${PROGRAM_FILE}"

if ! have_aarch64_binary_runtime; then
    echo "skipped runtime: missing AArch64 qemu/docker runner"
    exit 0
fi

run_aarch64_binary_with_available_runtime "${PROGRAM_FILE}" "${SYSROOT}" >/dev/null

echo "verified: AArch64 multi-object function-call ABI smoke emits .o files, links, and runs"
