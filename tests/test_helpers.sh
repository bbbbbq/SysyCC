#!/usr/bin/env bash

set -euo pipefail

acquire_build_lock() {
    local build_dir="$1"
    local lock_dir="${build_dir}/.sysycc_test_build.lock"
    local pid_file="${lock_dir}/pid"
    local wait_announced=0

    mkdir -p "${build_dir}"

    while ! mkdir "${lock_dir}" 2>/dev/null; do
        local owner_pid=""

        if [[ -f "${pid_file}" ]]; then
            owner_pid="$(<"${pid_file}")"
        fi

        if [[ -n "${owner_pid}" ]] && ! kill -0 "${owner_pid}" 2>/dev/null; then
            rm -rf "${lock_dir}"
            continue
        fi

        if [[ "${wait_announced}" -eq 0 ]]; then
            echo "==> Waiting for another local build to finish in ${build_dir}" >&2
            wait_announced=1
        fi

        sleep 1
    done

    printf '%s\n' "${BASHPID:-$$}" >"${pid_file}"
    printf '%s\n' "${lock_dir}"
}

release_build_lock() {
    local lock_dir="$1"
    local pid_file="${lock_dir}/pid"

    rm -f "${pid_file}"
    rmdir "${lock_dir}" 2>/dev/null || true
}

build_project() {
    local project_root="$1"
    local build_dir="$2"
    local cache_file="${build_dir}/CMakeCache.txt"
    local use_ccache=0
    local launcher_arg=""
    local generator_arg=""
    local lock_dir=""

    if [[ "${SYSYCC_TEST_SKIP_BUILD:-0}" == "1" ]]; then
        return 0
    fi

    if [[ "${SYSYCC_TEST_DISABLE_NINJA:-0}" != "1" ]] && command -v ninja >/dev/null 2>&1; then
        generator_arg="-G Ninja"
    fi

    if [[ "${SYSYCC_TEST_DISABLE_CCACHE:-0}" != "1" ]] && command -v ccache >/dev/null 2>&1; then
        use_ccache=1
        launcher_arg="-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache"
    fi

    lock_dir="$(acquire_build_lock "${build_dir}")"

    (
        trap 'release_build_lock "${lock_dir}"' EXIT

        if [[ ! -f "${cache_file}" || "${SYSYCC_TEST_FORCE_CONFIGURE:-0}" == "1" ]]; then
            cmake -S "${project_root}" -B "${build_dir}" ${generator_arg} ${launcher_arg}
        elif [[ "${use_ccache}" -eq 1 ]] && ! grep -q '^CMAKE_CXX_COMPILER_LAUNCHER:.*=ccache$' "${cache_file}"; then
            cmake -S "${project_root}" -B "${build_dir}" ${generator_arg} ${launcher_arg}
        fi

        if [[ -n "${SYSYCC_TEST_BUILD_JOBS:-}" ]]; then
            cmake --build "${build_dir}" --parallel "${SYSYCC_TEST_BUILD_JOBS}"
        else
            cmake --build "${build_dir}" --parallel
        fi
    )
}

build_and_link_ir_executable() {
    local ir_file="$1"
    local runtime_source="$2"
    local output_binary="$3"
    local runtime_dir
    local runtime_builtin_stub

    runtime_dir="$(dirname "${runtime_source}")"
    runtime_builtin_stub="${runtime_dir}/runtime_builtin_stub.ll"

    mkdir -p "$(dirname "${output_binary}")"
    if [[ -f "${runtime_builtin_stub}" ]]; then
        clang "${ir_file}" "${runtime_source}" "${runtime_builtin_stub}" \
            -fno-builtin -o "${output_binary}"
        return
    fi
    clang "${ir_file}" "${runtime_source}" -fno-builtin -o "${output_binary}"
}

copy_basic_frontend_outputs() {
    local build_dir="$1"
    local test_name="$2"
    local output_dir="$3"
    local result_dir="${build_dir}/intermediate_results"

    mkdir -p "${output_dir}"
    cp "${result_dir}/${test_name}.preprocessed.sy" "${output_dir}/"
    cp "${result_dir}/${test_name}.tokens.txt" "${output_dir}/"
    cp "${result_dir}/${test_name}.parse.txt" "${output_dir}/"
}

copy_optional_frontend_output() {
    local build_dir="$1"
    local test_name="$2"
    local suffix="$3"
    local output_dir="$4"
    local result_dir="${build_dir}/intermediate_results"
    local source_file="${result_dir}/${test_name}.${suffix}"

    mkdir -p "${output_dir}"
    if [[ -f "${source_file}" ]]; then
        cp "${source_file}" "${output_dir}/"
    fi
}

assert_file_nonempty() {
    local file_path="$1"

    if [[ ! -f "${file_path}" ]]; then
        echo "missing file: ${file_path}" >&2
        return 1
    fi

    if [[ ! -s "${file_path}" ]]; then
        echo "empty file: ${file_path}" >&2
        return 1
    fi
}

assert_basic_frontend_outputs() {
    local build_dir="$1"
    local test_name="$2"
    local result_dir="${build_dir}/intermediate_results"
    local preprocessed_file="${result_dir}/${test_name}.preprocessed.sy"
    local token_file="${result_dir}/${test_name}.tokens.txt"
    local parse_file="${result_dir}/${test_name}.parse.txt"

    assert_file_nonempty "${preprocessed_file}"
    assert_file_nonempty "${token_file}"
    assert_file_nonempty "${parse_file}"

    grep -q '^parse_success: true$' "${parse_file}"
    grep -q '^parse_message: parse succeeded$' "${parse_file}"
}

assert_compiler_fails_with_message() {
    local compiler_binary="$1"
    local input_file="$2"
    local expected_message="$3"

    local output
    local exit_code

    set +e
    output="$("${compiler_binary}" "${input_file}" 2>&1)"
    exit_code=$?
    set -e

    if [[ "${exit_code}" -eq 0 ]]; then
        echo "error: compiler unexpectedly succeeded for ${input_file}" >&2
        return 1
    fi

    if [[ "${output}" == *"${expected_message}"* ]]; then
        return 0
    fi

    local normalized_output
    normalized_output="$(printf '%s' "${output}" | sed -E 's#[^[:space:]]+:([0-9]+:[0-9]+-[0-9]+:[0-9]+)#\1#g')"
    if [[ "${normalized_output}" != *"${expected_message}"* ]]; then
        echo "error: expected semantic diagnostic not found" >&2
        echo "${output}" >&2
        return 1
    fi
}

assert_program_output() {
    local program_binary="$1"
    local input_file="$2"
    local expected_output_file="$3"

    local actual_output_file
    actual_output_file="$(mktemp)"
    if ! "${program_binary}" <"${input_file}" >"${actual_output_file}"; then
        echo "error: program execution failed: ${program_binary}" >&2
        rm -f "${actual_output_file}"
        return 1
    fi

    if ! diff -u "${expected_output_file}" "${actual_output_file}"; then
        echo "error: program output mismatch" >&2
        rm -f "${actual_output_file}"
        return 1
    fi

    rm -f "${actual_output_file}"
}
