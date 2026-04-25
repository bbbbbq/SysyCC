#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
CASE_BUILD_DIR="${SCRIPT_DIR}/build"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
MAIN_SOURCE="${SCRIPT_DIR}/run_aarch64_multi_object_external_symbol_main.c"
HELPER_SOURCE="${SCRIPT_DIR}/run_aarch64_multi_object_external_symbol_helper.c"
MAIN_LL="${CASE_BUILD_DIR}/${TEST_NAME}.main.ll"
HELPER_LL="${CASE_BUILD_DIR}/${TEST_NAME}.helper.ll"
MAIN_OBJECT="${CASE_BUILD_DIR}/${TEST_NAME}.main.o"
HELPER_OBJECT="${CASE_BUILD_DIR}/${TEST_NAME}.helper.o"
PROGRAM_FILE="${CASE_BUILD_DIR}/${TEST_NAME}.bin"
MAIN_RELOCATIONS="${CASE_BUILD_DIR}/${TEST_NAME}.main.relocations.txt"
HELPER_RELOCATIONS="${CASE_BUILD_DIR}/${TEST_NAME}.helper.relocations.txt"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

dump_aarch64_relocations() {
    local object_file="$1"
    local output_file="$2"
    local readelf_tool=""
    local objdump_tool=""

    readelf_tool="$(find_aarch64_readelf 2>/dev/null || true)"
    if [[ -n "${readelf_tool}" ]] &&
        "${readelf_tool}" -r "${object_file}" >"${output_file}" 2>&1; then
        return 0
    fi

    objdump_tool="$(command -v aarch64-linux-gnu-objdump 2>/dev/null || true)"
    if [[ -z "${objdump_tool}" ]]; then
        objdump_tool="$(command -v llvm-objdump 2>/dev/null || true)"
    fi
    if [[ -n "${objdump_tool}" ]] &&
        "${objdump_tool}" -dr "${object_file}" >"${output_file}" 2>&1; then
        return 0
    fi

    printf 'skipped: no target-capable relocation dumper available\n' \
        >"${output_file}"
    return 125
}

assert_relocation_symbol() {
    local object_file="$1"
    local output_file="$2"
    local symbol_pattern="$3"

    if ! dump_aarch64_relocations "${object_file}" "${output_file}"; then
        echo "skipped: no relocation inspection tool for ${object_file}"
        return 0
    fi
    if grep -Eq "${symbol_pattern}" "${output_file}"; then
        return 0
    fi

    echo "[FAIL] missing AArch64 relocation symbol pattern '${symbol_pattern}' in ${object_file}" >&2
    echo "---- relocation dump: ${output_file} ----" >&2
    cat "${output_file}" >&2
    return 1
}

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

assert_relocation_symbol "${MAIN_OBJECT}" "${MAIN_RELOCATIONS}" \
    'message_from_helper'
assert_relocation_symbol "${MAIN_OBJECT}" "${MAIN_RELOCATIONS}" 'puts'
assert_relocation_symbol "${HELPER_OBJECT}" "${HELPER_RELOCATIONS}" \
    'local_message'

run_aarch64_cc "${AARCH64_CC}" "${MAIN_OBJECT}" "${HELPER_OBJECT}" \
    -o "${PROGRAM_FILE}"
assert_file_nonempty "${PROGRAM_FILE}"

PROGRAM_OUTPUT="$(run_aarch64_binary_with_available_runtime "${PROGRAM_FILE}" "${SYSROOT}")"
if [[ "${PROGRAM_OUTPUT}" != "AArch64 external symbol" ]]; then
    echo "[FAIL] unexpected AArch64 external-symbol smoke output: '${PROGRAM_OUTPUT}'" >&2
    exit 1
fi

echo "verified: AArch64 multi-object external-symbol/rodata smoke emits PIC .o files, links libc, and runs"
