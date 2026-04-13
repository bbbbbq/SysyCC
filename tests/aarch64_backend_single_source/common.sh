#!/usr/bin/env bash

set -euo pipefail

single_source_stage_root() {
    cd "$(dirname "${BASH_SOURCE[0]}")" && pwd
}

single_source_case_id() {
    local source_rel="$1"
    local case_id

    case_id="${source_rel//\//__}"
    case_id="${case_id//./_}"
    printf '%s\n' "${case_id}"
}

run_single_source_snapshot_case() {
    local stage_root="$1"
    local project_root="$2"
    local build_dir="$3"
    local sysroot="$4"
    local aarch64_cc="$5"
    local host_clang="$6"
    local source_rel="$7"
    local c_std="$8"
    local argv_text="${9:-}"

    local case_id=""
    local source_file=""
    local case_build_dir=""
    local log_file=""
    local ll_file=""
    local sysycc_asm_file=""
    local sysycc_obj_file=""
    local clang_obj_file=""
    local sysycc_bin=""
    local clang_bin=""
    local sysycc_stdout=""
    local sysycc_stderr=""
    local sysycc_status=""
    local clang_stdout=""
    local clang_stderr=""
    local clang_status=""
    local -a runtime_args=()

    case_id="$(single_source_case_id "${source_rel}")"
    source_file="${stage_root}/upstream/${source_rel}"
    case_build_dir="${stage_root}/build/${case_id}"
    log_file="${build_dir}/test_logs/aarch64_backend_single_source_${case_id}.log"
    ll_file="${case_build_dir}/${case_id}.ll"
    sysycc_asm_file="${case_build_dir}/${case_id}.sysycc.s"
    sysycc_obj_file="${case_build_dir}/${case_id}.sysycc.o"
    clang_obj_file="${case_build_dir}/${case_id}.clang.o"
    sysycc_bin="${case_build_dir}/${case_id}.sysycc.bin"
    clang_bin="${case_build_dir}/${case_id}.clang.bin"
    sysycc_stdout="${case_build_dir}/${case_id}.sysycc.stdout"
    sysycc_stderr="${case_build_dir}/${case_id}.sysycc.stderr"
    sysycc_status="${case_build_dir}/${case_id}.sysycc.status"
    clang_stdout="${case_build_dir}/${case_id}.clang.stdout"
    clang_stderr="${case_build_dir}/${case_id}.clang.stderr"
    clang_status="${case_build_dir}/${case_id}.clang.status"

    mkdir -p "${case_build_dir}" "$(dirname "${log_file}")"
    if [[ -n "${argv_text}" && "${argv_text}" != "-" ]]; then
        read -r -a runtime_args <<<"${argv_text}"
    fi

    (
        echo "==> Running ${source_rel}"
        "${host_clang}" \
            --target=aarch64-unknown-linux-gnu \
            --sysroot="${sysroot}" \
            -std="${c_std}" \
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

        run_aarch64_cc "${aarch64_cc}" "${sysycc_obj_file}" -o "${sysycc_bin}"
        assert_file_nonempty "${sysycc_bin}"

        set +e
        if [[ "${#runtime_args[@]}" -eq 0 ]]; then
            run_aarch64_binary_with_available_runtime_args "${clang_bin}" "${sysroot}" "" \
                >"${clang_stdout}" 2>"${clang_stderr}"
        else
            run_aarch64_binary_with_available_runtime_args "${clang_bin}" "${sysroot}" "" \
                "${runtime_args[@]}" \
                >"${clang_stdout}" 2>"${clang_stderr}"
        fi
        echo "$?" >"${clang_status}"

        if [[ "${#runtime_args[@]}" -eq 0 ]]; then
            run_aarch64_binary_with_available_runtime_args "${sysycc_bin}" "${sysroot}" "" \
                >"${sysycc_stdout}" 2>"${sysycc_stderr}"
        else
            run_aarch64_binary_with_available_runtime_args "${sysycc_bin}" "${sysroot}" "" \
                "${runtime_args[@]}" \
                >"${sysycc_stdout}" 2>"${sysycc_stderr}"
        fi
        echo "$?" >"${sysycc_status}"
        set -e

        diff -u "${clang_stdout}" "${sysycc_stdout}"
        diff -u "${clang_stderr}" "${sysycc_stderr}"
        diff -u "${clang_status}" "${sysycc_status}"
    ) >"${log_file}" 2>&1
}
