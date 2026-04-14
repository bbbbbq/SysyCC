#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
MANIFEST_FILE="${SCRIPT_DIR}/manifest.txt"
BUILD_DIR="${PROJECT_ROOT}/build"
LIMIT="${1:-100}"
OUTPUT_FILE="${2:-${SCRIPT_DIR}/last_execute_batch_probe.tsv}"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

sysroot="$(find_aarch64_sysroot)"
aarch64_cc="$(find_aarch64_cc)"
sysycc_bin="${BUILD_DIR}/sysycc-aarch64c"

tmpdir="$(mktemp -d)"
trap 'rm -rf "${tmpdir}"' EXIT

git clone --depth 1 --filter=blob:none --sparse \
    https://github.com/llvm/llvm-test-suite.git \
    "${tmpdir}/llvm-test-suite" >/dev/null 2>&1
cd "${tmpdir}/llvm-test-suite"
git sparse-checkout set SingleSource/Regression/C/gcc-c-torture/execute >/dev/null 2>&1

all_cases_file="${tmpdir}/all_execute.txt"
imported_cases_file="${tmpdir}/imported_execute.txt"
remaining_cases_file="${tmpdir}/remaining_execute.txt"
batch_cases_file="${tmpdir}/batch_execute.txt"

find SingleSource/Regression/C/gcc-c-torture/execute -maxdepth 1 -name '*.c' | sort \
    >"${all_cases_file}"

awk -F'|' '
    NF >= 3 && $3 ~ /^SingleSource\/Regression\/C\/gcc-c-torture\/execute\/.*\.c$/ {
        print $3
    }
' "${MANIFEST_FILE}" | sort -u >"${imported_cases_file}"

comm -23 "${all_cases_file}" "${imported_cases_file}" >"${remaining_cases_file}"
head -n "${LIMIT}" "${remaining_cases_file}" >"${batch_cases_file}"

probe_case() {
    local src="$1"
    local base
    base="$(basename "${src}" .c)"
    local ll_file="${tmpdir}/${base}.ll"
    local asm_file="${tmpdir}/${base}.s"
    local clang_obj="${tmpdir}/${base}.clang.o"
    local sysycc_obj="${tmpdir}/${base}.sysycc.o"
    local clang_bin="${tmpdir}/${base}.clang.bin"
    local sysycc_bin_out="${tmpdir}/${base}.sysycc.bin"
    local clang_err="${tmpdir}/${base}.clang.err"
    local sysycc_err="${tmpdir}/${base}.sysycc.err"

    if ! clang --target=aarch64-unknown-linux-gnu --sysroot="${sysroot}" \
        -std=gnu89 -S -emit-llvm -O0 -Xclang -disable-O0-optnone \
        -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables \
        -fno-builtin "${src}" -o "${ll_file}" >/dev/null 2>&1; then
        printf 'COMPILE_SRC_FAIL|%s\n' "${src}"
        return 0
    fi
    if ! "${sysycc_bin}" -S "${ll_file}" -o "${asm_file}" >/dev/null 2>&1; then
        printf 'SYSYCC_FAIL|%s\n' "${src}"
        return 0
    fi
    if ! clang --target=aarch64-unknown-linux-gnu --sysroot="${sysroot}" \
        -c "${ll_file}" -o "${clang_obj}" >/dev/null 2>&1; then
        printf 'CLANG_LL_FAIL|%s\n' "${src}"
        return 0
    fi
    if ! run_aarch64_cc "${aarch64_cc}" -c "${asm_file}" -o "${sysycc_obj}" \
        >/dev/null 2>&1; then
        printf 'ASM_FAIL|%s\n' "${src}"
        return 0
    fi
    if ! run_aarch64_cc "${aarch64_cc}" "${clang_obj}" -o "${clang_bin}" \
        >/dev/null 2>&1; then
        printf 'CLANG_LINK_FAIL|%s\n' "${src}"
        return 0
    fi
    if ! run_aarch64_cc "${aarch64_cc}" "${sysycc_obj}" -o "${sysycc_bin_out}" \
        >/dev/null 2>&1; then
        printf 'SYSYCC_LINK_FAIL|%s\n' "${src}"
        return 0
    fi

    local clang_rc=0
    local sysycc_rc=0
    set +e
    run_aarch64_binary_with_available_runtime "${clang_bin}" "${sysroot}" \
        >/dev/null 2>"${clang_err}"
    clang_rc=$?
    run_aarch64_binary_with_available_runtime "${sysycc_bin_out}" "${sysroot}" \
        >/dev/null 2>"${sysycc_err}"
    sysycc_rc=$?
    set -e

    if [[ "${clang_rc}" -eq "${sysycc_rc}" ]] &&
        diff -u "${clang_err}" "${sysycc_err}" >/dev/null 2>&1; then
        printf 'PASS|%s\n' "${src}"
        return 0
    fi
    printf 'RUNTIME_MISMATCH|%s\n' "${src}"
}

{
    while IFS= read -r src; do
        probe_case "${src}"
    done <"${batch_cases_file}"
} >"${OUTPUT_FILE}"

echo "batch_size=$(wc -l < "${batch_cases_file}" | tr -d '[:space:]')"
echo "results_file=${OUTPUT_FILE}"

awk -F'|' '
    { counts[$1]++ }
    END {
        for (key in counts) {
            printf "%s=%d\n", key, counts[key];
        }
    }
' "${OUTPUT_FILE}" | sort

echo "first_failures:"
awk -F'|' '$1 != "PASS" { print $0 }' "${OUTPUT_FILE}" | sed -n '1,20p'
