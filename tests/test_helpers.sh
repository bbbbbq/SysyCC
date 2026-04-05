#!/usr/bin/env bash

set -euo pipefail

detect_cpu_count() {
    if command -v sysctl >/dev/null 2>&1; then
        local jobs
        jobs="$(sysctl -n hw.logicalcpu 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || true)"
        if [[ -n "${jobs}" ]]; then
            printf '%s\n' "${jobs}"
            return 0
        fi
    fi

    if command -v getconf >/dev/null 2>&1; then
        local jobs
        jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || true)"
        if [[ -n "${jobs}" ]]; then
            printf '%s\n' "${jobs}"
            return 0
        fi
    fi

    printf '4\n'
}

detect_total_memory_mib() {
    if command -v sysctl >/dev/null 2>&1; then
        local mem_bytes
        mem_bytes="$(sysctl -n hw.memsize 2>/dev/null || true)"
        if [[ "${mem_bytes}" =~ ^[0-9]+$ ]] && [[ "${mem_bytes}" -gt 0 ]]; then
            printf '%s\n' "$((mem_bytes / 1024 / 1024))"
            return 0
        fi
    fi

    if [[ -r /proc/meminfo ]]; then
        local mem_kib
        mem_kib="$(awk '/^MemTotal:/ { print $2; exit }' /proc/meminfo 2>/dev/null || true)"
        if [[ "${mem_kib}" =~ ^[0-9]+$ ]] && [[ "${mem_kib}" -gt 0 ]]; then
            printf '%s\n' "$((mem_kib / 1024))"
            return 0
        fi
    fi

    printf '0\n'
}

detect_default_build_jobs() {
    local cpu_count
    local memory_mib

    cpu_count="$(detect_cpu_count)"
    memory_mib="$(detect_total_memory_mib)"

    if [[ "${memory_mib}" -le 0 ]]; then
        if [[ "${cpu_count}" -le 2 ]]; then
            printf '%s\n' "${cpu_count}"
        else
            printf '4\n'
        fi
        return 0
    fi

    if [[ "${memory_mib}" -le 8192 ]]; then
        printf '1\n'
        return 0
    fi

    if [[ "${memory_mib}" -le 16384 ]]; then
        if [[ "${cpu_count}" -le 2 ]]; then
            printf '%s\n' "${cpu_count}"
        else
            printf '2\n'
        fi
        return 0
    fi

    if [[ "${memory_mib}" -le 32768 ]]; then
        if [[ "${cpu_count}" -le 4 ]]; then
            printf '%s\n' "${cpu_count}"
        else
            printf '4\n'
        fi
        return 0
    fi

    if [[ "${cpu_count}" -le 6 ]]; then
        printf '%s\n' "${cpu_count}"
        return 0
    fi

    printf '6\n'
}

detect_default_heavy_tool_jobs() {
    local cpu_count
    local memory_mib

    cpu_count="$(detect_cpu_count)"
    memory_mib="$(detect_total_memory_mib)"

    if [[ "${memory_mib}" -le 0 ]]; then
        if [[ "${cpu_count}" -le 2 ]]; then
            printf '1\n'
        else
            printf '2\n'
        fi
        return 0
    fi

    if [[ "${memory_mib}" -le 8192 ]]; then
        printf '1\n'
        return 0
    fi

    if [[ "${memory_mib}" -le 16384 ]]; then
        printf '2\n'
        return 0
    fi

    if [[ "${memory_mib}" -le 32768 ]]; then
        printf '3\n'
        return 0
    fi

    if [[ "${cpu_count}" -le 4 ]]; then
        printf '2\n'
        return 0
    fi

    printf '4\n'
}

setup_test_host_tool_wrappers() {
    if [[ "${SYSYCC_TEST_DISABLE_HOST_TOOL_WRAPPERS:-0}" == "1" ]]; then
        return 0
    fi

    if [[ -z "${PROJECT_ROOT:-}" ]]; then
        return 0
    fi

    if [[ "${SYSYCC_TEST_TOOL_WRAPPERS_READY:-0}" == "1" ]]; then
        return 0
    fi

    mkdir -p "${PROJECT_ROOT}/build/.sysycc_test_wrappers/bin"

    if [[ -z "${SYSYCC_TEST_REAL_CLANG:-}" ]]; then
        SYSYCC_TEST_REAL_CLANG="$(command -v clang)"
        export SYSYCC_TEST_REAL_CLANG
    fi

    if [[ -z "${SYSYCC_TEST_REAL_CLANGXX:-}" ]]; then
        SYSYCC_TEST_REAL_CLANGXX="$(command -v clang++)"
        export SYSYCC_TEST_REAL_CLANGXX
    fi

    export SYSYCC_TEST_ACTIVE=1
    export SYSYCC_TEST_HEAVY_TOOL_JOBS="${SYSYCC_TEST_HEAVY_TOOL_JOBS:-$(detect_default_heavy_tool_jobs)}"
    export SYSYCC_TEST_HEAVY_TOOL_LOCK_ROOT="${SYSYCC_TEST_HEAVY_TOOL_LOCK_ROOT:-${PROJECT_ROOT}/build/.sysycc_test_heavy_tool_slots}"

    cat >"${PROJECT_ROOT}/build/.sysycc_test_wrappers/bin/clang" <<EOF
#!/usr/bin/env bash
# SYSYCC test wrapper
export SYSYCC_TEST_WRAPPED_TOOL_NAME="clang"
export SYSYCC_TEST_WRAPPED_ARGV0="clang"
exec "${PROJECT_ROOT}/scripts/test_heavy_tool_wrapper.sh" "${SYSYCC_TEST_REAL_CLANG}" "\$@"
EOF

    cat >"${PROJECT_ROOT}/build/.sysycc_test_wrappers/bin/clang++" <<EOF
#!/usr/bin/env bash
# SYSYCC test wrapper
export SYSYCC_TEST_WRAPPED_TOOL_NAME="clang++"
export SYSYCC_TEST_WRAPPED_ARGV0="clang++"
exec "${PROJECT_ROOT}/scripts/test_heavy_tool_wrapper.sh" "${SYSYCC_TEST_REAL_CLANGXX}" "\$@"
EOF

    chmod +x "${PROJECT_ROOT}/build/.sysycc_test_wrappers/bin/clang" \
        "${PROJECT_ROOT}/build/.sysycc_test_wrappers/bin/clang++"

    case ":${PATH}:" in
        *":${PROJECT_ROOT}/build/.sysycc_test_wrappers/bin:"*)
            ;;
        *)
            export PATH="${PROJECT_ROOT}/build/.sysycc_test_wrappers/bin:${PATH}"
            ;;
    esac

    export SYSYCC_TEST_TOOL_WRAPPERS_READY=1
}

is_sysycc_test_wrapper_file() {
    local file_path="$1"
    LC_ALL=C grep -a -q 'SYSYCC test wrapper' "${file_path}" 2>/dev/null
}

reset_sysycc_test_binary_wrapper_state() {
    local build_dir="$1"
    local wrapper_dir="${build_dir}/.sysycc_test_wrappers"
    local wrapper_path="${build_dir}/SysyCC"
    local real_binary_path="${wrapper_dir}/SysyCC.real"

    if is_sysycc_test_wrapper_file "${real_binary_path}"; then
        rm -f "${real_binary_path}"
    fi

    if is_sysycc_test_wrapper_file "${wrapper_path}" && [[ ! -e "${real_binary_path}" ]]; then
        rm -f "${wrapper_path}"
    fi
}

install_sysycc_test_binary_wrapper() {
    local build_dir="$1"
    local wrapper_dir="${build_dir}/.sysycc_test_wrappers"
    local wrapper_path="${build_dir}/SysyCC"
    local real_binary_path="${wrapper_dir}/SysyCC.real"
    local install_lock_dir="${wrapper_dir}/.install.lock"

    if [[ "${SYSYCC_TEST_DISABLE_HOST_TOOL_WRAPPERS:-0}" == "1" ]]; then
        return 0
    fi

    mkdir -p "${wrapper_dir}"
    acquire_named_lock "${install_lock_dir}"

    (
        trap 'release_named_lock "${install_lock_dir}"' EXIT

        if [[ ! -e "${wrapper_path}" && ! -e "${real_binary_path}" ]]; then
            return 0
        fi

        if is_sysycc_test_wrapper_file "${real_binary_path}"; then
            rm -f "${real_binary_path}"
        fi

        if [[ -e "${wrapper_path}" ]] &&
           ! is_sysycc_test_wrapper_file "${wrapper_path}" &&
           [[ ! -e "${real_binary_path}" ]]; then
            mv "${wrapper_path}" "${real_binary_path}"
        fi

        if [[ ! -e "${real_binary_path}" ]]; then
            return 0
        fi

        cat >"${wrapper_path}" <<EOF
#!/usr/bin/env bash
# SYSYCC test wrapper
export SYSYCC_TEST_WRAPPED_TOOL_NAME="SysyCC"
export SYSYCC_TEST_WRAPPED_ARGV0="SysyCC"
exec "${PROJECT_ROOT}/scripts/test_heavy_tool_wrapper.sh" "${real_binary_path}" "\$@"
EOF

        chmod +x "${wrapper_path}"
    )
}

acquire_named_lock() {
    local lock_dir="$1"
    local pid_file="${lock_dir}/pid"

    mkdir -p "$(dirname "${lock_dir}")"

    while ! mkdir "${lock_dir}" 2>/dev/null; do
        local owner_pid=""

        owner_pid="$(cat "${pid_file}" 2>/dev/null || true)"
        if [[ -n "${owner_pid}" ]] && ! kill -0 "${owner_pid}" 2>/dev/null; then
            rm -rf "${lock_dir}"
            continue
        fi

        sleep 0.1
    done

    printf '%s\n' "${BASHPID:-$$}" >"${pid_file}"
}

release_named_lock() {
    local lock_dir="$1"
    local pid_file="${lock_dir}/pid"

    rm -f "${pid_file}"
    rmdir "${lock_dir}" 2>/dev/null || true
}

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

prune_stale_build_outputs() {
    local project_root="$1"
    local build_dir="$2"
    local stale_ir_object_dir="${build_dir}/CMakeFiles/SysyCC.dir/src/backend/ir/passes"
    local stale_generated_parser_object="${build_dir}/CMakeFiles/SysyCC.dir/src/frontend/parser/parser_generated.cpp.o"
    local stale_generated_parser_dep="${build_dir}/CMakeFiles/SysyCC.dir/src/frontend/parser/parser_generated.cpp.o.d"
    local stale_generated_lexer_object="${build_dir}/CMakeFiles/SysyCC.dir/src/frontend/lexer/lexer_scanner.cpp.o"
    local stale_generated_lexer_dep="${build_dir}/CMakeFiles/SysyCC.dir/src/frontend/lexer/lexer_scanner.cpp.o.d"

    # Path-only refactors can leave old object trees behind. Some IR tests link
    # every compiler object except main.cpp, so stale objects must be removed.
    if [[ ! -d "${project_root}/src/backend/ir/passes" ]] && [[ -d "${stale_ir_object_dir}" ]]; then
        rm -rf "${stale_ir_object_dir}"
    fi

    if [[ ! -f "${project_root}/src/frontend/parser/parser_generated.cpp" ]]; then
        rm -f "${stale_generated_parser_object}" "${stale_generated_parser_dep}"
    fi

    if [[ ! -f "${project_root}/src/frontend/lexer/lexer_scanner.cpp" ]]; then
        rm -f "${stale_generated_lexer_object}" "${stale_generated_lexer_dep}"
    fi
}

build_project() {
    local project_root="$1"
    local build_dir="$2"
    local cache_file="${build_dir}/CMakeCache.txt"
    local build_jobs="${SYSYCC_TEST_BUILD_JOBS:-$(detect_default_build_jobs)}"
    local use_ccache=0
    local launcher_arg=""
    local generator_arg=""
    local lock_dir=""

    setup_test_host_tool_wrappers

    if [[ "${SYSYCC_TEST_SKIP_BUILD:-0}" == "1" ]]; then
        install_sysycc_test_binary_wrapper "${build_dir}"
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

        reset_sysycc_test_binary_wrapper_state "${build_dir}"

        if [[ ! -f "${cache_file}" || "${SYSYCC_TEST_FORCE_CONFIGURE:-0}" == "1" ]]; then
            cmake -S "${project_root}" -B "${build_dir}" ${generator_arg} ${launcher_arg}
        elif [[ "${use_ccache}" -eq 1 ]] && ! grep -q '^CMAKE_CXX_COMPILER_LAUNCHER:.*=ccache$' "${cache_file}"; then
            cmake -S "${project_root}" -B "${build_dir}" ${generator_arg} ${launcher_arg}
        fi

        prune_stale_build_outputs "${project_root}" "${build_dir}"

        cmake --build "${build_dir}" --parallel "${build_jobs}"
        install_sysycc_test_binary_wrapper "${build_dir}"
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

strip_diagnostic_source_excerpts() {
    printf '%s' "$1" | sed '/^  /d'
}

parse_expected_diagnostic_shape() {
    local expected_message="$1"
    EXPECTED_STAGE=""
    EXPECTED_SEVERITY=""
    EXPECTED_MESSAGE_BODY=""
    EXPECTED_SPAN=""

    local remainder="${expected_message}"
    local stage=""
    for stage in semantic preprocess parser lexer compiler ast; do
        if [[ "${remainder}" == "${stage} "* ]]; then
            EXPECTED_STAGE="${stage}"
            remainder="${remainder#${stage} }"
            break
        fi
    done

    case "${remainder}" in
        error:\ *|warning:\ *|note:\ *)
            EXPECTED_SEVERITY="${remainder%%:*}"
            remainder="${remainder#${EXPECTED_SEVERITY}: }"
            EXPECTED_MESSAGE_BODY="${remainder}"
            if [[ "${remainder}" == *" at "* ]]; then
                EXPECTED_MESSAGE_BODY="${remainder% at *}"
                EXPECTED_SPAN="${remainder##* at }"
            fi
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

parse_expected_span() {
    local span="$1"
    EXPECTED_SPAN_PATH=""
    EXPECTED_LINE_BEGIN=""
    EXPECTED_COL_BEGIN=""
    EXPECTED_LINE_END=""
    EXPECTED_COL_END=""

    if [[ -z "${span}" ]]; then
        return 1
    fi

    if [[ "${span}" =~ ^(.+):([0-9]+):([0-9]+)-([0-9]+):([0-9]+)$ ]]; then
        EXPECTED_SPAN_PATH="${BASH_REMATCH[1]}"
        EXPECTED_LINE_BEGIN="${BASH_REMATCH[2]}"
        EXPECTED_COL_BEGIN="${BASH_REMATCH[3]}"
        EXPECTED_LINE_END="${BASH_REMATCH[4]}"
        EXPECTED_COL_END="${BASH_REMATCH[5]}"
        return 0
    fi

    if [[ "${span}" =~ ^([0-9]+):([0-9]+)-([0-9]+):([0-9]+)$ ]]; then
        EXPECTED_LINE_BEGIN="${BASH_REMATCH[1]}"
        EXPECTED_COL_BEGIN="${BASH_REMATCH[2]}"
        EXPECTED_LINE_END="${BASH_REMATCH[3]}"
        EXPECTED_COL_END="${BASH_REMATCH[4]}"
        return 0
    fi

    if [[ "${span}" =~ ^(.+):([0-9]+):([0-9]+)$ ]]; then
        EXPECTED_SPAN_PATH="${BASH_REMATCH[1]}"
        EXPECTED_LINE_BEGIN="${BASH_REMATCH[2]}"
        EXPECTED_COL_BEGIN="${BASH_REMATCH[3]}"
        EXPECTED_LINE_END="${BASH_REMATCH[2]}"
        EXPECTED_COL_END="${BASH_REMATCH[3]}"
        return 0
    fi

    if [[ "${span}" =~ ^([0-9]+):([0-9]+)$ ]]; then
        EXPECTED_LINE_BEGIN="${BASH_REMATCH[1]}"
        EXPECTED_COL_BEGIN="${BASH_REMATCH[2]}"
        EXPECTED_LINE_END="${BASH_REMATCH[1]}"
        EXPECTED_COL_END="${BASH_REMATCH[2]}"
        return 0
    fi

    return 1
}

infer_primary_input_path() {
    local compiler_args=("$@")
    local arg=""
    local input_path=""
    for arg in "${compiler_args[@]}"; do
        if [[ "${arg}" == -* ]]; then
            continue
        fi
        if [[ -f "${arg}" ]]; then
            input_path="${arg}"
        fi
    done
    printf '%s' "${input_path}"
}

assert_diagnostic_span_rendering() {
    local output="$1"
    local severity="$2"
    local line_begin="$3"
    local col_begin="$4"
    local line_end="$5"
    local col_end="$6"

    if [[ "${severity}" == "note" || "${line_begin}" != "${line_end}" ]]; then
        return 0
    fi

    local width=$((col_end - col_begin + 1))
    if (( width <= 1 )); then
        grep -Eq '^[[:space:]]+\^$' <<<"${output}"
        return
    fi

    grep -Eq "^[[:space:]]+\\^~{$((width - 1))}$" <<<"${output}"
}

assert_legacy_stage_expectation_matches_output() {
    local output="$1"
    shift
    local expected_message="$1"
    shift
    local compiler_args=("$@")

    if ! parse_expected_diagnostic_shape "${expected_message}"; then
        return 1
    fi

    if [[ -z "${EXPECTED_SPAN}" ]]; then
        [[ "${output}" == *"${EXPECTED_SEVERITY}: ${EXPECTED_MESSAGE_BODY}"* ]]
        return
    fi

    if ! parse_expected_span "${EXPECTED_SPAN}"; then
        return 1
    fi

    local expected_path="${EXPECTED_SPAN_PATH}"
    if [[ -z "${expected_path}" ]]; then
        expected_path="$(infer_primary_input_path "${compiler_args[@]}")"
    fi

    local expected_header
    if [[ -n "${expected_path}" ]]; then
        expected_header="${expected_path}:${EXPECTED_LINE_BEGIN}:${EXPECTED_COL_BEGIN}: ${EXPECTED_SEVERITY}: ${EXPECTED_MESSAGE_BODY}"
    else
        expected_header="${EXPECTED_LINE_BEGIN}:${EXPECTED_COL_BEGIN}: ${EXPECTED_SEVERITY}: ${EXPECTED_MESSAGE_BODY}"
    fi

    if [[ "${output}" != *"${expected_header}"* ]]; then
        return 1
    fi

    assert_diagnostic_span_rendering \
        "${output}" \
        "${EXPECTED_SEVERITY}" \
        "${EXPECTED_LINE_BEGIN}" \
        "${EXPECTED_COL_BEGIN}" \
        "${EXPECTED_LINE_END}" \
        "${EXPECTED_COL_END}"
}

normalize_diagnostic_headers_to_legacy_path_line() {
    strip_diagnostic_source_excerpts "$1" |
        sed -E 's#^(.*):([0-9]+):([0-9]+): (error|warning|note): (.*)$#\1:\2: \5#'
}

normalize_diagnostic_headers_to_line_col() {
    strip_diagnostic_source_excerpts "$1" |
        sed -E 's#^.*:([0-9]+):([0-9]+): (error|warning|note): (.*)$#\1:\2: \3: \4#'
}

legacy_stage_expectation_matches() {
    local output="$1"
    local expected_message="$2"
    local remainder="${expected_message}"
    local severity=""
    local message=""
    local span=""
    local span_begin=""
    local stage=""

    for stage in semantic preprocess parser lexer compiler ast; do
        if [[ "${remainder}" == "${stage} "* ]]; then
            remainder="${remainder#${stage} }"
            break
        fi
    done

    case "${remainder}" in
        error:\ *|warning:\ *|note:\ *)
            severity="${remainder%%:*}"
            remainder="${remainder#${severity}: }"
            message="${remainder}"
            if [[ "${remainder}" == *" at "* ]]; then
                message="${remainder% at *}"
                span="${remainder##* at }"
            fi
            ;;
        *)
            return 1
            ;;
    esac

    if [[ "${output}" != *"${severity}: ${message}"* ]]; then
        return 1
    fi

    if [[ -z "${span}" ]]; then
        return 0
    fi

    span_begin="$(printf '%s' "${span}" | sed -E \
        -e 's#^.*:([0-9]+:[0-9]+)-([0-9]+:[0-9]+)$#\1#' \
        -e 't done' \
        -e 's#^([0-9]+:[0-9]+)-([0-9]+:[0-9]+)$#\1#' \
        -e 't done' \
        -e 's#^.*:([0-9]+:[0-9]+)$#\1#' \
        -e 't done' \
        -e 's#^([0-9]+:[0-9]+)$#\1#' \
        -e ':done')"
    if [[ -z "${span_begin}" || "${span_begin}" == "${span}" ]]; then
        return 1
    fi

    [[ "${output}" == *"${span_begin}: ${severity}: ${message}"* ]]
}

assert_compiler_fails_with_message() {
    local compiler_binary="$1"
    shift
    local expected_message="${@: -1}"
    local compiler_args=("${@:1:$#-1}")

    local output
    local exit_code

    set +e
    output="$("${compiler_binary}" "${compiler_args[@]}" 2>&1)"
    exit_code=$?
    set -e

    if [[ "${exit_code}" -eq 0 ]]; then
        echo "error: compiler unexpectedly succeeded for ${compiler_args[*]}" >&2
        return 1
    fi

    if assert_legacy_stage_expectation_matches_output \
        "${output}" \
        "${expected_message}" \
        "${compiler_args[@]}"; then
        return 0
    fi

    if [[ "${output}" == *"${expected_message}"* ]]; then
        return 0
    fi

    local legacy_path_line_output
    legacy_path_line_output="$(normalize_diagnostic_headers_to_legacy_path_line \
        "${output}")"
    if [[ "${legacy_path_line_output}" == *"${expected_message}"* ]]; then
        return 0
    fi

    local normalized_output
    normalized_output="$(normalize_diagnostic_headers_to_line_col "${output}")"
    if [[ "${normalized_output}" == *"${expected_message}"* ]] ||
       legacy_stage_expectation_matches "${normalized_output}" \
           "${expected_message}"; then
        return 0
    fi

    echo "error: expected semantic diagnostic not found" >&2
    echo "${output}" >&2
    return 1
}

assert_compiler_succeeds_with_message() {
    local compiler_binary="$1"
    shift
    local expected_message="${@: -1}"
    local compiler_args=("${@:1:$#-1}")

    local output
    local exit_code

    set +e
    output="$("${compiler_binary}" "${compiler_args[@]}" 2>&1)"
    exit_code=$?
    set -e

    if [[ "${exit_code}" -ne 0 ]]; then
        echo "error: compiler unexpectedly failed for ${compiler_args[*]}" >&2
        echo "${output}" >&2
        return 1
    fi

    if assert_legacy_stage_expectation_matches_output \
        "${output}" \
        "${expected_message}" \
        "${compiler_args[@]}"; then
        return 0
    fi

    if [[ "${output}" == *"${expected_message}"* ]]; then
        return 0
    fi

    local legacy_path_line_output
    legacy_path_line_output="$(normalize_diagnostic_headers_to_legacy_path_line \
        "${output}")"
    if [[ "${legacy_path_line_output}" == *"${expected_message}"* ]]; then
        return 0
    fi

    local normalized_output
    normalized_output="$(normalize_diagnostic_headers_to_line_col "${output}")"
    if [[ "${normalized_output}" == *"${expected_message}"* ]] ||
       legacy_stage_expectation_matches "${normalized_output}" \
           "${expected_message}"; then
        return 0
    fi

    echo "error: expected compiler diagnostic not found" >&2
    echo "${output}" >&2
    return 1
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
