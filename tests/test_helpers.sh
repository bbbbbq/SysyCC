#!/usr/bin/env bash

set -euo pipefail

build_project() {
    local project_root="$1"
    local build_dir="$2"

    cmake -S "${project_root}" -B "${build_dir}"
    cmake --build "${build_dir}"
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

    if [[ "${output}" != *"${expected_message}"* ]]; then
        echo "error: expected semantic diagnostic not found" >&2
        echo "${output}" >&2
        return 1
    fi
}
