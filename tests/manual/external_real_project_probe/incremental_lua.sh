#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"

DOCKER_CONTAINER="${SYSYCC_REAL_PROJECT_DOCKER_CONTAINER:-qemu_dev}"
DOCKER_PROJECT_ROOT="${SYSYCC_REAL_PROJECT_DOCKER_PROJECT_ROOT:-/code/SysyCC}"
DOCKER_BUILD_DIR="${SYSYCC_REAL_PROJECT_DOCKER_BUILD_DIR:-${DOCKER_PROJECT_ROOT}/build-qemu-real-project-probe}"
WORK_DIR="${SYSYCC_REAL_PROJECT_WORK_DIR:-${PROJECT_ROOT}/build/external-real-project-probe}"
DEFAULT_LLVM_DIR="/usr/lib/llvm-18/lib/cmake/llvm"
LLVM_DIR_OVERRIDE="${SYSYCC_REAL_PROJECT_LLVM_DIR:-}"
BUILD_TYPE="${SYSYCC_REAL_PROJECT_BUILD_TYPE:-RelWithDebInfo}"
LUA_REPO="${SYSYCC_LUA_REPO:-https://github.com/lua/lua.git}"
REQUESTED_SOURCES="${SYSYCC_LUA_INCREMENTAL_SOURCES:-${*:-}}"
DEFAULT_SOURCE="${SYSYCC_LUA_INCREMENTAL_DEFAULT_SOURCE:-lvm.c}"

log() {
    printf '==> %s\n' "$*"
}

run_in_docker() {
    if ! command -v docker >/dev/null 2>&1; then
        echo "error: docker is required for Lua incremental validation" >&2
        exit 1
    fi
    if ! docker inspect "${DOCKER_CONTAINER}" >/dev/null 2>&1; then
        echo "error: docker container '${DOCKER_CONTAINER}' is not available" >&2
        echo "hint: run tests/manual/external_real_project_probe/verify.sh first" >&2
        exit 1
    fi
    docker exec \
        -e SYSYCC_REAL_PROJECT_IN_DOCKER=1 \
        -e SYSYCC_REAL_PROJECT_DOCKER_BUILD_DIR="${DOCKER_BUILD_DIR}" \
        -e SYSYCC_REAL_PROJECT_BUILD_TYPE="${BUILD_TYPE}" \
        -e SYSYCC_LUA_REPO="${LUA_REPO}" \
        -e SYSYCC_LUA_INCREMENTAL_SOURCES="${REQUESTED_SOURCES}" \
        -e SYSYCC_LUA_INCREMENTAL_DEFAULT_SOURCE="${DEFAULT_SOURCE}" \
        -w "${DOCKER_PROJECT_ROOT}" \
        "${DOCKER_CONTAINER}" \
        bash "tests/manual/external_real_project_probe/incremental_lua.sh"
}

cmake_configure_args() {
    printf '%s\n' -S "${PROJECT_ROOT}" -B "$1" -G Ninja
    printf '%s\n' "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}"
    if [[ -n "${LLVM_DIR_OVERRIDE}" ]]; then
        printf '%s\n' "-DLLVM_DIR=${LLVM_DIR_OVERRIDE}"
    elif [[ -d "${DEFAULT_LLVM_DIR}" ]]; then
        printf '%s\n' "-DLLVM_DIR=${DEFAULT_LLVM_DIR}"
    fi
    printf '%s\n' "-DLLVM_LINK_LLVM_DYLIB=ON"
}

clone_or_update_repo() {
    local repo_url="$1"
    local destination="$2"

    if [[ -d "${destination}/.git" ]]; then
        git -C "${destination}" remote set-url origin "${repo_url}"
        git -C "${destination}" fetch --depth 1 origin HEAD
        return
    fi

    git clone --depth 1 "${repo_url}" "${destination}"
}

write_timing_wrapper() {
    local wrapper="$1"
    cat >"${wrapper}" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

real_cc="${SYSYCC_TIMED_CC_REAL:?}"
log_file="${SYSYCC_TIMED_CC_LOG:?}"

source_file="-"
output_file="-"
mode="link"
prev=""
for arg in "$@"; do
    if [[ "${prev}" == "-o" ]]; then
        output_file="${arg}"
        prev=""
        continue
    fi
    if [[ "${arg}" == "-o" ]]; then
        prev="-o"
        continue
    fi
    if [[ "${arg}" == *.c ]]; then
        source_file="${arg}"
    fi
    if [[ "${arg}" == "-c" ]]; then
        mode="compile"
    fi
done

start_ns="$(python3 -c 'import time; print(time.perf_counter_ns())')"
set +e
"${real_cc}" "$@"
rc=$?
set -e
end_ns="$(python3 -c 'import time; print(time.perf_counter_ns())')"
elapsed_ms="$(python3 - "${start_ns}" "${end_ns}" <<'PY'
import sys
start = int(sys.argv[1])
end = int(sys.argv[2])
print(f"{(end - start) / 1_000_000:.3f}")
PY
)"
printf '%s\t%s\t%s\t%s\t%s\n' "${elapsed_ms}" "${rc}" "${mode}" "${source_file}" "${output_file}" >>"${log_file}"
exit "${rc}"
EOF
    chmod +x "${wrapper}"
}

write_incremental_report() {
    local raw_log="$1"
    local report="$2"
    local sources_text="$3"

    {
        echo "# Lua SysyCC Incremental Validation"
        echo
        echo "- Requested sources: ${sources_text}"
        echo
        echo "| Rank | ms | rc | mode | source | output |"
        echo "| ---: | ---: | ---: | --- | --- | --- |"
        sort -t $'\t' -k1,1nr "${raw_log}" |
            awk -F '\t' '{ printf "| %d | %s | %s | %s | `%s` | `%s` |\n", NR, $1, $2, $3, $4, $5 }'
        echo
        awk -F '\t' '
            {
                total += $1
                if ($3 == "compile") {
                    compiles += 1
                    compile_total += $1
                }
                if ($3 == "link") {
                    links += 1
                    link_total += $1
                }
            }
            END {
                printf "Total commands: %d\n\n", NR
                printf "Compile commands: %d (%.3f ms)\n\n", compiles, compile_total
                printf "Link commands: %d (%.3f ms)\n\n", links, link_total
                printf "Total ms: %.3f\n", total
            }
        ' "${raw_log}"
    } >"${report}"
}

normalize_lua_source() {
    local source="$1"
    source="${source#./}"
    source="$(basename "${source}")"
    if [[ "${source}" != *.c ]]; then
        source="${source}.c"
    fi
    printf '%s\n' "${source}"
}

detect_dirty_lua_sources() {
    local source_dir="$1"
    git -C "${source_dir}" status --porcelain -- '*.c' |
        awk '{ print $NF }' |
        while IFS= read -r source; do
            [[ -n "${source}" ]] && normalize_lua_source "${source}"
        done |
        sort -u
}

select_incremental_sources() {
    local source_dir="$1"
    local selected=()
    local source=""

    if [[ -n "${REQUESTED_SOURCES}" ]]; then
        for source in ${REQUESTED_SOURCES}; do
            selected+=("$(normalize_lua_source "${source}")")
        done
    else
        while IFS= read -r source; do
            [[ -n "${source}" ]] && selected+=("${source}")
        done < <(detect_dirty_lua_sources "${source_dir}")
        if [[ "${#selected[@]}" -eq 0 && -n "${DEFAULT_SOURCE}" ]]; then
            selected+=("$(normalize_lua_source "${DEFAULT_SOURCE}")")
        fi
    fi

    printf '%s\n' "${selected[@]}"
}

ensure_lua_baseline() {
    local source_dir="$1"
    local compiler="$2"

    if [[ -x "${source_dir}/lua" && -f "${source_dir}/liblua.a" ]]; then
        return
    fi

    log "Lua baseline is missing; building once without clean"
    make -C "${source_dir}" -j1 \
        CC="${compiler}" \
        AR="ar rc" \
        RANLIB="ranlib"
}

run_native_incremental() {
    local docker_build_dir="${SYSYCC_REAL_PROJECT_DOCKER_BUILD_DIR:-${PROJECT_ROOT}/build-docker-real-project-probe}"
    local external_dir="${WORK_DIR}/repos"
    local lua_dir="${external_dir}/lua"
    local reports_dir="${WORK_DIR}/reports"
    local wrapper="${reports_dir}/sysycc-lua-incremental-cc"
    local raw_log="${reports_dir}/lua_incremental_times.tsv"
    local report="${reports_dir}/lua_incremental_times.md"
    local build_log="${reports_dir}/lua_incremental_build.log"
    local pass_report_dir="${reports_dir}/lua_incremental_pass_reports"
    local previous_pass_report_dir="${reports_dir}/lua_incremental_pass_reports.prev"
    local diff_report_dir="${reports_dir}/lua_incremental_pass_report_diffs"

    log "configuring SysyCC inside Docker"
    cmake_args=()
    while IFS= read -r arg; do
        cmake_args+=("${arg}")
    done < <(cmake_configure_args "${docker_build_dir}")
    cmake "${cmake_args[@]}"
    cmake --build "${docker_build_dir}" --parallel "${SYSYCC_REAL_PROJECT_BUILD_JOBS:-4}"

    mkdir -p "${external_dir}" "${reports_dir}"
    log "preparing Lua"
    clone_or_update_repo "${LUA_REPO}" "${lua_dir}"

    ensure_lua_baseline "${lua_dir}" "${docker_build_dir}/compiler"

    selected_sources=()
    while IFS= read -r source; do
        [[ -n "${source}" ]] && selected_sources+=("${source}")
    done < <(select_incremental_sources "${lua_dir}")

    if [[ "${#selected_sources[@]}" -eq 0 ]]; then
        log "no explicit or dirty Lua .c sources; relying on Make dependencies"
    else
        log "forcing Lua object rebuild for: ${selected_sources[*]}"
        for source in "${selected_sources[@]}"; do
            if [[ ! -f "${lua_dir}/${source}" ]]; then
                echo "error: Lua source '${source}' does not exist" >&2
                exit 1
            fi
            rm -f "${lua_dir}/${source%.c}.o"
        done
    fi

    : >"${raw_log}"
    rm -rf "${previous_pass_report_dir}" "${diff_report_dir}"
    if [[ -d "${pass_report_dir}" ]]; then
        mv "${pass_report_dir}" "${previous_pass_report_dir}"
    fi
    write_timing_wrapper "${wrapper}"

    log "incrementally rebuilding Lua and relinking"
    if ! SYSYCC_TIMED_CC_REAL="${docker_build_dir}/compiler" \
        SYSYCC_TIMED_CC_LOG="${raw_log}" \
        SYSYCC_PASS_REPORT_DIR="${pass_report_dir}" \
            make -C "${lua_dir}" -j1 \
                CC="${wrapper}" \
                AR="ar rc" \
                RANLIB="ranlib" >"${build_log}" 2>&1; then
        tail -80 "${build_log}" >&2
        return 1
    fi

    local sources_text
    if [[ "${#selected_sources[@]}" -eq 0 ]]; then
        sources_text="make-detected"
    else
        sources_text="${selected_sources[*]}"
    fi
    write_incremental_report "${raw_log}" "${report}" "${sources_text}"

    if [[ -d "${previous_pass_report_dir}" ]]; then
        mkdir -p "${diff_report_dir}"
        local new_report
        local previous_report
        local diff_report
        for new_report in "${pass_report_dir}"/*.md; do
            [[ -f "${new_report}" ]] || continue
            previous_report="${previous_pass_report_dir}/$(basename "${new_report}")"
            if [[ ! -f "${previous_report}" ]]; then
                continue
            fi
            diff_report="${diff_report_dir}/$(basename "${new_report}")"
            python3 "${SCRIPT_DIR}/diff_pass_reports.py" \
                "${previous_report}" \
                "${new_report}" \
                -o "${diff_report}"
        done
    fi

    log "running Lua behavior smoke"
    SYSYCC_REAL_PROJECT_IN_DOCKER=1 \
        SYSYCC_LUA_BINARY="${lua_dir}/lua" \
        bash "${SCRIPT_DIR}/lua_smoke.sh"

    echo "wrote ${report}"
    echo "wrote ${build_log}"
    echo "wrote pass reports under ${pass_report_dir}"
    if [[ -d "${diff_report_dir}" ]] &&
        find "${diff_report_dir}" -maxdepth 1 -type f -name '*.md' | grep -q .; then
        echo "wrote pass report diffs under ${diff_report_dir}"
    fi
}

if [[ "${SYSYCC_REAL_PROJECT_IN_DOCKER:-0}" != "1" ]]; then
    run_in_docker
else
    run_native_incremental
fi
