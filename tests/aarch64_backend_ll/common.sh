#!/usr/bin/env bash

set -euo pipefail

run_aarch64_backend_ll_case() {
    local case_dir="$1"
    local project_root="$2"
    local build_dir="$3"

    local case_name
    case_name="$(basename "${case_dir}")"

    local source_file="${case_dir}/${case_name}.c"
    local expected_stdout_file="${case_dir}/expected.stdout"
    local expected_exit_file="${case_dir}/expected.exit"
    local case_build_dir="${case_dir}/build"
    local ll_file="${case_build_dir}/${case_name}.ll"
    local sysycc_asm_file="${case_build_dir}/${case_name}.sysycc.s"
    local sysycc_obj_file="${case_build_dir}/${case_name}.sysycc.o"
    local clang_obj_file="${case_build_dir}/${case_name}.clang.o"
    local sysycc_bin="${case_build_dir}/${case_name}.sysycc.bin"
    local clang_bin="${case_build_dir}/${case_name}.clang.bin"
    local sysycc_stdout="${case_build_dir}/${case_name}.sysycc.stdout"
    local sysycc_stderr="${case_build_dir}/${case_name}.sysycc.stderr"
    local sysycc_status="${case_build_dir}/${case_name}.sysycc.status"
    local clang_stdout="${case_build_dir}/${case_name}.clang.stdout"
    local clang_stderr="${case_build_dir}/${case_name}.clang.stderr"
    local clang_status="${case_build_dir}/${case_name}.clang.status"

    local host_clang=""
    local aarch64_cc=""
    local sysroot=""
    local qemu=""

    host_clang="${SYSYCC_HOST_CLANG:-$(command -v clang 2>/dev/null || true)}"
    if [[ -z "${host_clang}" ]]; then
        echo "missing host clang for ${case_name}" >&2
        return 1
    fi

    sysroot="$(find_aarch64_sysroot 2>/dev/null || true)"
    qemu="$(find_qemu_aarch64 2>/dev/null || true)"
    aarch64_cc="$(find_aarch64_cc 2>/dev/null || true)"

    mkdir -p "${case_build_dir}"

    "${host_clang}" \
        --target=aarch64-unknown-linux-gnu \
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

    "${build_dir}/sysycc-aarch64c" -S "${ll_file}" -o "${sysycc_asm_file}"
    assert_file_nonempty "${sysycc_asm_file}"

    if [[ -z "${sysroot}" ]]; then
        echo "skipped runtime parity for ${case_name}: missing AArch64 sysroot"
        return 0
    fi
    if [[ -z "${aarch64_cc}" ]]; then
        echo "skipped runtime parity for ${case_name}: missing AArch64 C compiler"
        return 0
    fi

    "${host_clang}" \
        --target=aarch64-unknown-linux-gnu \
        --sysroot="${sysroot}" \
        -c "${ll_file}" \
        -o "${clang_obj_file}"
    assert_file_nonempty "${clang_obj_file}"

    run_aarch64_cc "${aarch64_cc}" -c "${sysycc_asm_file}" -o "${sysycc_obj_file}"
    assert_file_nonempty "${sysycc_obj_file}"

    run_aarch64_cc "${aarch64_cc}" "${clang_obj_file}" -o "${clang_bin}"
    assert_file_nonempty "${clang_bin}"

    run_aarch64_cc "${aarch64_cc}" "${sysycc_obj_file}" \
        -o "${sysycc_bin}"
    assert_file_nonempty "${sysycc_bin}"

    if [[ -z "${qemu}" ]]; then
        echo "skipped runtime parity for ${case_name}: missing qemu-aarch64"
        return 0
    fi

    set +e
    QEMU_LD_PREFIX="${sysroot}" "${qemu}" -L "${sysroot}" "${clang_bin}" \
        >"${clang_stdout}" 2>"${clang_stderr}"
    echo "$?" >"${clang_status}"

    QEMU_LD_PREFIX="${sysroot}" "${qemu}" -L "${sysroot}" "${sysycc_bin}" \
        >"${sysycc_stdout}" 2>"${sysycc_stderr}"
    echo "$?" >"${sysycc_status}"
    set -e

    diff -u "${expected_stdout_file}" "${clang_stdout}"
    diff -u "${expected_stdout_file}" "${sysycc_stdout}"
    diff -u "${clang_stdout}" "${sysycc_stdout}"

    diff -u "${expected_exit_file}" "${clang_status}"
    diff -u "${expected_exit_file}" "${sysycc_status}"
    diff -u "${clang_status}" "${sysycc_status}"
}
