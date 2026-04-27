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
MUJS_GITHUB_REPO="${SYSYCC_MUJS_GITHUB_REPO:-https://github.com/ccxvii/mujs.git}"
MUJS_FALLBACK_REPO="${SYSYCC_MUJS_FALLBACK_REPO:-https://codeberg.org/ccxvii/mujs.git}"
PROFILE_TARGETS="${SYSYCC_REAL_PROJECT_PROFILE_TARGETS:-${*:-lua mujs}}"

log() {
    printf '==> %s\n' "$*"
}

run_in_docker() {
    if ! command -v docker >/dev/null 2>&1; then
        echo "error: docker is required for real-project compile-time profiling" >&2
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
        -e SYSYCC_REAL_PROJECT_PROFILE_TARGETS="${PROFILE_TARGETS}" \
        -e SYSYCC_LUA_REPO="${LUA_REPO}" \
        -e SYSYCC_MUJS_GITHUB_REPO="${MUJS_GITHUB_REPO}" \
        -e SYSYCC_MUJS_FALLBACK_REPO="${MUJS_FALLBACK_REPO}" \
        -w "${DOCKER_PROJECT_ROOT}" \
        "${DOCKER_CONTAINER}" \
        bash "tests/manual/external_real_project_probe/profile_compile_times.sh"
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
    local branch="${3:-}"

    if [[ -d "${destination}/.git" ]]; then
        git -C "${destination}" remote set-url origin "${repo_url}"
        git -C "${destination}" fetch --depth 1 origin "${branch:-HEAD}"
    else
        if [[ -n "${branch}" ]]; then
            git clone --depth 1 --branch "${branch}" "${repo_url}" "${destination}"
        else
            git clone --depth 1 "${repo_url}" "${destination}"
        fi
    fi
}

repo_has_c_sources() {
    local repo_dir="$1"
    find "${repo_dir}" -maxdepth 2 -type f -name '*.c' | grep -q .
}

prepare_mujs_repo() {
    local destination="$1"

    if [[ -d "${destination}/.git" ]] && repo_has_c_sources "${destination}"; then
        return
    fi

    rm -rf "${destination}"
    if git clone --depth 1 "${MUJS_GITHUB_REPO}" "${destination}" &&
        repo_has_c_sources "${destination}"; then
        return
    fi

    rm -rf "${destination}"
    git clone --depth 1 "${MUJS_FALLBACK_REPO}" "${destination}"
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

write_report() {
    local project_name="$1"
    local raw_log="$2"
    local report_file="$3"

    {
        echo "# ${project_name} SysyCC Compile-Time Profile"
        echo
        echo "| Rank | ms | rc | mode | source | output |"
        echo "| ---: | ---: | ---: | --- | --- | --- |"
        awk -F '\t' '$3 == "compile" { print $0 }' "${raw_log}" |
            sort -t $'\t' -k1,1nr |
            awk -F '\t' '{ printf "| %d | %s | %s | %s | `%s` | `%s` |\n", NR, $1, $2, $3, $4, $5 }'
        echo
        awk -F '\t' '
            $3 == "compile" {
                count += 1
                total += $1
                if ($1 > max) {
                    max = $1
                    max_source = $4
                }
            }
            END {
                printf "Total compile entries: %d\n\n", count
                printf "Total compile ms: %.3f\n\n", total
                printf "Slowest source: `%s` (%.3f ms)\n", max_source, max
            }
        ' "${raw_log}"
    } >"${report_file}"
}

profile_lua() {
    local source_dir="$1"
    local compiler="$2"
    local reports_dir="$3"
    local wrapper="$4"
    local raw_log="${reports_dir}/lua_compile_times.tsv"
    local report="${reports_dir}/lua_compile_times.md"
    local build_log="${reports_dir}/lua_build.log"
    local pass_report_dir="${reports_dir}/lua_pass_reports"

    log "profiling Lua per-file compile times"
    : >"${raw_log}"
    rm -rf "${pass_report_dir}"
    make -C "${source_dir}" clean >/dev/null 2>&1 || true
    if ! SYSYCC_TIMED_CC_REAL="${compiler}" \
        SYSYCC_TIMED_CC_LOG="${raw_log}" \
        SYSYCC_PASS_REPORT_DIR="${pass_report_dir}" \
            make -C "${source_dir}" -j1 \
                CC="${wrapper}" \
                AR="ar rc" \
                RANLIB="ranlib" >"${build_log}" 2>&1; then
        tail -80 "${build_log}" >&2
        return 1
    fi
    write_report "Lua" "${raw_log}" "${report}"
    echo "wrote ${report}"
    echo "wrote ${build_log}"
    echo "wrote pass reports under ${pass_report_dir}"
}

profile_mujs() {
    local source_dir="$1"
    local compiler="$2"
    local reports_dir="$3"
    local wrapper="$4"
    local raw_log="${reports_dir}/mujs_compile_times.tsv"
    local report="${reports_dir}/mujs_compile_times.md"
    local build_log="${reports_dir}/mujs_build.log"
    local pass_report_dir="${reports_dir}/mujs_pass_reports"

    log "profiling MuJS per-file compile times"
    : >"${raw_log}"
    rm -rf "${pass_report_dir}"
    make -C "${source_dir}" clean >/dev/null 2>&1 || true
    if ! SYSYCC_TIMED_CC_REAL="${compiler}" \
        SYSYCC_TIMED_CC_LOG="${raw_log}" \
        SYSYCC_PASS_REPORT_DIR="${pass_report_dir}" \
            make -C "${source_dir}" -j1 release \
                CC="${wrapper}" \
                HAVE_READLINE=no >"${build_log}" 2>&1; then
        tail -80 "${build_log}" >&2
        return 1
    fi
    write_report "MuJS" "${raw_log}" "${report}"
    echo "wrote ${report}"
    echo "wrote ${build_log}"
    echo "wrote pass reports under ${pass_report_dir}"
}

run_native_profile() {
    local docker_build_dir="${SYSYCC_REAL_PROJECT_DOCKER_BUILD_DIR:-${PROJECT_ROOT}/build-docker-real-project-probe}"
    local external_dir="${WORK_DIR}/repos"
    local lua_dir="${external_dir}/lua"
    local mujs_dir="${external_dir}/mujs"
    local reports_dir="${WORK_DIR}/reports"
    local wrapper="${reports_dir}/sysycc-timed-cc"

    log "configuring SysyCC inside Docker"
    mapfile -t cmake_args < <(cmake_configure_args "${docker_build_dir}")
    cmake "${cmake_args[@]}"
    cmake --build "${docker_build_dir}" --parallel "${SYSYCC_REAL_PROJECT_BUILD_JOBS:-4}"

    mkdir -p "${external_dir}" "${reports_dir}"
    if [[ " ${PROFILE_TARGETS} " == *" lua "* ]]; then
        log "preparing Lua"
        clone_or_update_repo "${LUA_REPO}" "${lua_dir}"
    fi
    if [[ " ${PROFILE_TARGETS} " == *" mujs "* ]]; then
        log "preparing MuJS"
        prepare_mujs_repo "${mujs_dir}"
    fi

    write_timing_wrapper "${wrapper}"

    if [[ " ${PROFILE_TARGETS} " == *" lua "* ]]; then
        profile_lua "${lua_dir}" "${docker_build_dir}/compiler" "${reports_dir}" "${wrapper}"
    fi
    if [[ " ${PROFILE_TARGETS} " == *" mujs "* ]]; then
        profile_mujs "${mujs_dir}" "${docker_build_dir}/compiler" "${reports_dir}" "${wrapper}"
    fi
}

if [[ "${SYSYCC_REAL_PROJECT_IN_DOCKER:-0}" != "1" ]]; then
    run_in_docker
else
    run_native_profile
fi
