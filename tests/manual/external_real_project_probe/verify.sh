#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"

DOCKER_CONTAINER="${SYSYCC_REAL_PROJECT_DOCKER_CONTAINER:-qemu_dev}"
DOCKER_IMAGE="${SYSYCC_REAL_PROJECT_DOCKER_IMAGE:-sysycc-real-project-probe:ubuntu24.04}"
DOCKER_BASE_IMAGE="${SYSYCC_REAL_PROJECT_DOCKER_BASE_IMAGE:-ubuntu:24.04}"
DOCKER_PROJECT_ROOT="${SYSYCC_REAL_PROJECT_DOCKER_PROJECT_ROOT:-/code/SysyCC}"
DOCKER_BUILD_DIR="${SYSYCC_REAL_PROJECT_DOCKER_BUILD_DIR:-${DOCKER_PROJECT_ROOT}/build-qemu-real-project-probe}"
WORK_DIR="${SYSYCC_REAL_PROJECT_WORK_DIR:-${PROJECT_ROOT}/build/external-real-project-probe}"
DOCKERFILE="${WORK_DIR}/Dockerfile"
DEFAULT_LLVM_DIR="/usr/lib/llvm-18/lib/cmake/llvm"
LLVM_DIR_OVERRIDE="${SYSYCC_REAL_PROJECT_LLVM_DIR:-}"

LUA_REPO="${SYSYCC_LUA_REPO:-https://github.com/lua/lua.git}"
MUJS_GITHUB_REPO="${SYSYCC_MUJS_GITHUB_REPO:-https://github.com/ccxvii/mujs.git}"
MUJS_FALLBACK_REPO="${SYSYCC_MUJS_FALLBACK_REPO:-https://codeberg.org/ccxvii/mujs.git}"

log() {
    printf '==> %s\n' "$*"
}

ensure_docker_image() {
    mkdir -p "${WORK_DIR}"
    cat >"${DOCKERFILE}" <<EOF
FROM ${DOCKER_BASE_IMAGE}
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update \\
    && apt-get install -y --no-install-recommends \\
        ca-certificates \\
        clang \\
        cmake \\
        bison \\
        flex \\
        g++ \\
        gcc \\
        git \\
        llvm-dev \\
        make \\
        ninja-build \\
        python3 \\
    && rm -rf /var/lib/apt/lists/*
EOF
    docker build -t "${DOCKER_IMAGE}" -f "${DOCKERFILE}" "${WORK_DIR}"
}

run_in_docker() {
    if docker inspect "${DOCKER_CONTAINER}" >/dev/null 2>&1; then
        docker exec \
            -e SYSYCC_REAL_PROJECT_IN_DOCKER=1 \
            -e SYSYCC_REAL_PROJECT_DOCKER_BUILD_DIR="${DOCKER_BUILD_DIR}" \
            -e SYSYCC_LUA_REPO="${LUA_REPO}" \
            -e SYSYCC_MUJS_GITHUB_REPO="${MUJS_GITHUB_REPO}" \
            -e SYSYCC_MUJS_FALLBACK_REPO="${MUJS_FALLBACK_REPO}" \
            -w "${DOCKER_PROJECT_ROOT}" \
            "${DOCKER_CONTAINER}" \
            bash "tests/manual/external_real_project_probe/verify.sh"
        return
    fi

    ensure_docker_image
    docker run --rm --init \
        --user "$(id -u):$(id -g)" \
        -e SYSYCC_REAL_PROJECT_IN_DOCKER=1 \
        -e SYSYCC_REAL_PROJECT_DOCKER_BUILD_DIR="/workspace/build-docker-real-project-probe" \
        -e SYSYCC_LUA_REPO="${LUA_REPO}" \
        -e SYSYCC_MUJS_GITHUB_REPO="${MUJS_GITHUB_REPO}" \
        -e SYSYCC_MUJS_FALLBACK_REPO="${MUJS_FALLBACK_REPO}" \
        -v "${PROJECT_ROOT}:/workspace" \
        -w /workspace \
        "${DOCKER_IMAGE}" \
        bash "tests/manual/external_real_project_probe/verify.sh"
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

cmake_configure_args() {
    printf '%s\n' -S "${PROJECT_ROOT}" -B "$1" -G Ninja
    if [[ -n "${LLVM_DIR_OVERRIDE}" ]]; then
        printf '%s\n' "-DLLVM_DIR=${LLVM_DIR_OVERRIDE}"
    elif [[ -d "${DEFAULT_LLVM_DIR}" ]]; then
        printf '%s\n' "-DLLVM_DIR=${DEFAULT_LLVM_DIR}"
    fi
    printf '%s\n' "-DLLVM_LINK_LLVM_DYLIB=ON"
}

prepare_mujs_repo() {
    local destination="$1"

    rm -rf "${destination}"
    if git clone --depth 1 "${MUJS_GITHUB_REPO}" "${destination}" &&
        repo_has_c_sources "${destination}"; then
        return
    fi

    rm -rf "${destination}"
    git clone --depth 1 "${MUJS_FALLBACK_REPO}" "${destination}"
}

run_lua_probe() {
    local source_dir="$1"
    local compiler="$2"

    log "building Lua with SysyCC"
    make -C "${source_dir}" clean >/dev/null 2>&1 || true
    make -C "${source_dir}" -j1 \
        CC="${compiler}" \
        AR="ar rc" \
        RANLIB="ranlib"

    local output
    output="$("${source_dir}/lua" -e 'print("lua-ok", 21 + 12)')"
    if [[ "${output}" != $'lua-ok\t33' && "${output}" != "lua-ok 33" ]]; then
        echo "unexpected Lua smoke output: ${output}" >&2
        exit 1
    fi
}

run_mujs_probe() {
    local source_dir="$1"
    local compiler="$2"

    log "building MuJS release with SysyCC"
    make -C "${source_dir}" clean >/dev/null 2>&1 || true
    make -C "${source_dir}" -j1 release \
        CC="${compiler}" \
        HAVE_READLINE=no

    local script_file="${source_dir}/build/release/sysycc-smoke.js"
    printf 'print("mujs-ok", 21 + 12)\n' >"${script_file}"

    local output
    output="$("${source_dir}/build/release/mujs" "${script_file}")"
    if [[ "${output}" != "mujs-ok 33" ]]; then
        echo "unexpected MuJS smoke output: ${output}" >&2
        exit 1
    fi
}

run_native_probe() {
    local docker_build_dir="${SYSYCC_REAL_PROJECT_DOCKER_BUILD_DIR:-${PROJECT_ROOT}/build-docker-real-project-probe}"
    local external_dir="${WORK_DIR}/repos"
    local lua_dir="${external_dir}/lua"
    local mujs_dir="${external_dir}/mujs"

    log "configuring SysyCC inside Docker"
    mapfile -t cmake_args < <(cmake_configure_args "${docker_build_dir}")
    cmake "${cmake_args[@]}"
    cmake --build "${docker_build_dir}" --parallel "${SYSYCC_REAL_PROJECT_BUILD_JOBS:-4}"

    mkdir -p "${external_dir}"
    log "cloning Lua"
    rm -rf "${lua_dir}"
    git clone --depth 1 "${LUA_REPO}" "${lua_dir}"

    log "cloning MuJS"
    prepare_mujs_repo "${mujs_dir}"

    run_lua_probe "${lua_dir}" "${docker_build_dir}/compiler"
    run_mujs_probe "${mujs_dir}" "${docker_build_dir}/compiler"

    local lua_commit
    local mujs_commit
    lua_commit="$(git -C "${lua_dir}" rev-parse --short HEAD)"
    mujs_commit="$(git -C "${mujs_dir}" rev-parse --short HEAD)"
    echo "verified: SysyCC builds and runs Lua (${lua_commit}) and MuJS (${mujs_commit}) inside Docker"
}

if [[ "${SYSYCC_REAL_PROJECT_IN_DOCKER:-0}" != "1" ]]; then
    if ! command -v docker >/dev/null 2>&1; then
        echo "error: docker is required for the external real-project probe" >&2
        exit 1
    fi
    run_in_docker
else
    run_native_probe
fi
