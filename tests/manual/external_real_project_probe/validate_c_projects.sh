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
PROJECTS="${SYSYCC_REAL_C_PROJECTS:-${*:-lua zlib sqlite libpng git openssl}}"
FAIL_FAST="${SYSYCC_REAL_PROJECT_FAIL_FAST:-0}"

LUA_REPO="${SYSYCC_LUA_REPO:-https://github.com/lua/lua.git}"
ZLIB_REPO="${SYSYCC_ZLIB_REPO:-https://github.com/madler/zlib.git}"
SQLITE_REPO="${SYSYCC_SQLITE_REPO:-https://github.com/sqlite/sqlite.git}"
LIBPNG_REPO="${SYSYCC_LIBPNG_REPO:-https://github.com/pnggroup/libpng.git}"
GIT_REPO="${SYSYCC_GIT_REPO:-https://github.com/git/git.git}"
OPENSSL_REPO="${SYSYCC_OPENSSL_REPO:-https://github.com/openssl/openssl.git}"

log() {
    printf '==> %s\n' "$*"
}

run_in_docker() {
    if ! command -v docker >/dev/null 2>&1; then
        echo "error: docker is required for the real C project matrix" >&2
        exit 1
    fi
    if ! docker inspect "${DOCKER_CONTAINER}" >/dev/null 2>&1; then
        echo "error: docker container '${DOCKER_CONTAINER}' is not available" >&2
        exit 1
    fi
    docker exec \
        -e SYSYCC_REAL_PROJECT_IN_DOCKER=1 \
        -e SYSYCC_REAL_PROJECT_DOCKER_BUILD_DIR="${DOCKER_BUILD_DIR}" \
        -e SYSYCC_REAL_PROJECT_BUILD_TYPE="${BUILD_TYPE}" \
        -e SYSYCC_REAL_C_PROJECTS="${PROJECTS}" \
        -e SYSYCC_REAL_PROJECT_FAIL_FAST="${FAIL_FAST}" \
        -e SYSYCC_LUA_REPO="${LUA_REPO}" \
        -e SYSYCC_ZLIB_REPO="${ZLIB_REPO}" \
        -e SYSYCC_SQLITE_REPO="${SQLITE_REPO}" \
        -e SYSYCC_LIBPNG_REPO="${LIBPNG_REPO}" \
        -e SYSYCC_GIT_REPO="${GIT_REPO}" \
        -e SYSYCC_OPENSSL_REPO="${OPENSSL_REPO}" \
        -w "${DOCKER_PROJECT_ROOT}" \
        "${DOCKER_CONTAINER}" \
        bash "tests/manual/external_real_project_probe/validate_c_projects.sh"
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
        if [[ -n "${branch}" ]]; then
            git -C "${destination}" checkout -q FETCH_HEAD
        fi
        return
    fi

    if [[ -n "${branch}" ]]; then
        git clone --depth 1 --branch "${branch}" "${repo_url}" "${destination}"
    else
        git clone --depth 1 "${repo_url}" "${destination}"
    fi
}

project_commit() {
    local source_dir="$1"
    if [[ -d "${source_dir}/.git" ]]; then
        git -C "${source_dir}" rev-parse --short HEAD
    else
        printf '-'
    fi
}

append_summary() {
    local name="$1"
    local status="$2"
    local commit="$3"
    local log_file="$4"
    printf '| %s | %s | `%s` | `%s` |\n' \
        "${name}" "${status}" "${commit}" "${log_file}" >>"${SUMMARY_ROWS_FILE}"
}

run_logged() {
    local name="$1"
    local log_file="$2"
    shift 2

    log "running ${name}"
    {
        printf '## %s\n\n' "${name}"
        printf '$'
        printf ' %q' "$@"
        printf '\n\n'
        "$@"
    } >"${log_file}" 2>&1
}

run_project() {
    local name="$1"
    local source_dir="$2"
    local log_file="${REPORTS_DIR}/${name}.log"
    shift 2

    local status="PASS"
    set +e
    run_logged "${name}" "${log_file}" "$@"
    local rc=$?
    set -e
    if [[ "${rc}" -ne 0 ]]; then
        status="FAIL(${rc})"
        tail -80 "${log_file}" >&2 || true
    fi
    append_summary "${name}" "${status}" "$(project_commit "${source_dir}")" "${log_file}"
    if [[ "${rc}" -ne 0 && "${FAIL_FAST}" == "1" ]]; then
        exit "${rc}"
    fi
    return "${rc}"
}

clean_make_project() {
    local source_dir="$1"
    make -C "${source_dir}" clean >/dev/null 2>&1 || true
    make -C "${source_dir}" distclean >/dev/null 2>&1 || true
}

build_lua() {
    local source_dir="$1"
    local compiler="$2"

    clean_make_project "${source_dir}"
    make -C "${source_dir}" -j1 CC="${compiler}" AR="ar rc" RANLIB="ranlib"
    local output
    output="$("${source_dir}/lua" -e 'print("lua-ok", 21 + 12)')"
    [[ "${output}" == $'lua-ok\t33' || "${output}" == "lua-ok 33" ]]
}

build_zlib() {
    local source_dir="$1"
    local compiler="$2"

    clean_make_project "${source_dir}"
    (cd "${source_dir}" && CC="${compiler}" ./configure --static)
    make -C "${source_dir}" -j1 test
}

build_sqlite() {
    local source_dir="$1"
    local compiler="$2"
    local build_dir="${source_dir}/build-sysycc"

    rm -rf "${build_dir}"
    mkdir -p "${build_dir}"
    (cd "${build_dir}" &&
        CC="${compiler}" ../configure --disable-shared --enable-static --disable-readline)
    make -C "${build_dir}" -j1 sqlite3
    local output
    output="$("${build_dir}/sqlite3" :memory: 'select 40 + 2;')"
    [[ "${output}" == "42" ]]
}

build_libpng() {
    local source_dir="$1"
    local compiler="$2"
    local build_dir="${source_dir}/build-sysycc"
    local zlib_dir
    zlib_dir="$(cd "${source_dir}/.." && pwd)/zlib"
    local zlib_args=()
    if [[ -f "${zlib_dir}/libz.a" && -f "${zlib_dir}/zlib.h" ]]; then
        zlib_args+=("-DZLIB_LIBRARY=${zlib_dir}/libz.a")
        zlib_args+=("-DZLIB_INCLUDE_DIR=${zlib_dir}")
    fi

    rm -rf "${build_dir}"
    cmake -S "${source_dir}" -B "${build_dir}" -G Ninja \
        -DCMAKE_C_COMPILER="${compiler}" \
        -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON \
        -DPNG_SHARED=ON \
        -DPNG_STATIC=OFF \
        -DPNG_TESTS=ON \
        "${zlib_args[@]}"
    cmake --build "${build_dir}" --target pngtest --parallel 1
    cp "${source_dir}/pngtest.png" "${build_dir}/pngtest.png"
    (cd "${build_dir}" && LD_LIBRARY_PATH="${build_dir}:${LD_LIBRARY_PATH:-}" ./pngtest)
}

build_git() {
    local source_dir="$1"
    local compiler="$2"

    clean_make_project "${source_dir}"
    make -C "${source_dir}" -j1 git \
        CC="${compiler}" \
        NO_GETTEXT=1 \
        NO_OPENSSL=1 \
        NO_CURL=1 \
        NO_EXPAT=1 \
        NO_ICONV=1 \
        NO_PERL=1 \
        NO_PYTHON=1 \
        NO_TCLTK=1
    "${source_dir}/git" version | grep -q '^git version '
}

openssl_target() {
    case "$(uname -m)" in
    aarch64|arm64)
        printf 'linux-aarch64'
        ;;
    x86_64|amd64)
        printf 'linux-x86_64'
        ;;
    *)
        printf 'linux-generic64'
        ;;
    esac
}

build_openssl() {
    local source_dir="$1"
    local compiler="$2"

    git -C "${source_dir}" clean -fdx >/dev/null 2>&1 || true
    (cd "${source_dir}" &&
        CC="${compiler}" ./Configure "$(openssl_target)" no-asm no-shared no-tests)
    make -C "${source_dir}" -j1 build_sw
    "${source_dir}/apps/openssl" version | grep -q '^OpenSSL '
}

run_native_matrix() {
    local docker_build_dir="${SYSYCC_REAL_PROJECT_DOCKER_BUILD_DIR:-${PROJECT_ROOT}/build-docker-real-project-probe}"
    local external_dir="${WORK_DIR}/repos"
    local compiler="${docker_build_dir}/compiler"

    REPORTS_DIR="${WORK_DIR}/reports/c-project-matrix"
    SUMMARY_ROWS_FILE="${REPORTS_DIR}/summary.rows"
    SUMMARY_FILE="${REPORTS_DIR}/summary.md"
    export REPORTS_DIR SUMMARY_ROWS_FILE

    mkdir -p "${external_dir}" "${REPORTS_DIR}"
    : >"${SUMMARY_ROWS_FILE}"

    log "configuring SysyCC"
    mapfile -t cmake_args < <(cmake_configure_args "${docker_build_dir}")
    cmake "${cmake_args[@]}"
    cmake --build "${docker_build_dir}" --parallel "${SYSYCC_REAL_PROJECT_BUILD_JOBS:-4}"

    local failed=0
    for project in ${PROJECTS}; do
        case "${project}" in
        lua)
            local dir="${external_dir}/lua"
            clone_or_update_repo "${LUA_REPO}" "${dir}"
            run_project lua "${dir}" bash -lc "$(printf 'build_lua %q %q' "${dir}" "${compiler}")" || failed=1
            ;;
        zlib)
            local dir="${external_dir}/zlib"
            clone_or_update_repo "${ZLIB_REPO}" "${dir}"
            run_project zlib "${dir}" bash -lc "$(printf 'build_zlib %q %q' "${dir}" "${compiler}")" || failed=1
            ;;
        sqlite)
            local dir="${external_dir}/sqlite"
            clone_or_update_repo "${SQLITE_REPO}" "${dir}"
            run_project sqlite "${dir}" bash -lc "$(printf 'build_sqlite %q %q' "${dir}" "${compiler}")" || failed=1
            ;;
        libpng)
            local dir="${external_dir}/libpng"
            clone_or_update_repo "${LIBPNG_REPO}" "${dir}"
            run_project libpng "${dir}" bash -lc "$(printf 'build_libpng %q %q' "${dir}" "${compiler}")" || failed=1
            ;;
        git)
            local dir="${external_dir}/git"
            clone_or_update_repo "${GIT_REPO}" "${dir}"
            run_project git "${dir}" bash -lc "$(printf 'build_git %q %q' "${dir}" "${compiler}")" || failed=1
            ;;
        openssl)
            local dir="${external_dir}/openssl"
            clone_or_update_repo "${OPENSSL_REPO}" "${dir}"
            run_project openssl "${dir}" bash -lc "$(printf 'build_openssl %q %q' "${dir}" "${compiler}")" || failed=1
            ;;
        *)
            echo "error: unknown project '${project}'" >&2
            exit 1
            ;;
        esac
    done

    {
        echo "# SysyCC Real C Project Matrix"
        echo
        echo "- Generated: $(date -u +"%Y-%m-%dT%H:%M:%SZ")"
        echo "- Compiler: \`${compiler}\`"
        echo "- Projects: \`${PROJECTS}\`"
        echo
        echo "| Project | Status | Commit | Log |"
        echo "| --- | --- | --- | --- |"
        cat "${SUMMARY_ROWS_FILE}"
    } >"${SUMMARY_FILE}"
    echo "wrote ${SUMMARY_FILE}"
    return "${failed}"
}

if [[ "${SYSYCC_REAL_PROJECT_IN_DOCKER:-0}" != "1" ]]; then
    run_in_docker
else
    export -f build_lua build_zlib build_sqlite build_libpng build_git build_openssl
    export -f clean_make_project openssl_target
    run_native_matrix
fi
