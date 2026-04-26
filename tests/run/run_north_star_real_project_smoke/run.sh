#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
CASE_BUILD_DIR="${SCRIPT_DIR}/build"
TEMPLATE_DIR="${SCRIPT_DIR}/project"
MAKE_PROJECT_DIR="${CASE_BUILD_DIR}/make-project"
CMAKE_SOURCE_DIR="${CASE_BUILD_DIR}/cmake-source"
CMAKE_BUILD_DIR="${CASE_BUILD_DIR}/cmake-build"
TOOLCHAIN_FILE="${CASE_BUILD_DIR}/sysycc-toolchain.cmake"
HOST_APP="${CASE_BUILD_DIR}/northstar_host_full_compile"
EXPECTED_OUTPUT="northstar: score=79 weighted=279"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

find_archive_tool() {
    command -v aarch64-linux-gnu-ar 2>/dev/null ||
        command -v llvm-ar 2>/dev/null ||
        command -v ar
}

find_ranlib_tool() {
    command -v aarch64-linux-gnu-ranlib 2>/dev/null ||
        command -v llvm-ranlib 2>/dev/null ||
        command -v ranlib 2>/dev/null ||
        true
}

run_and_check_output() {
    local program="$1"
    local output=""

    output="$("${program}")"
    if [[ "${output}" != "${EXPECTED_OUTPUT}" ]]; then
        echo "unexpected north-star output from ${program}" >&2
        echo "expected: ${EXPECTED_OUTPUT}" >&2
        echo "actual:   ${output}" >&2
        exit 1
    fi
}

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
rm -rf "${CASE_BUILD_DIR}"
mkdir -p "${CASE_BUILD_DIR}"
ARCHIVE_TOOL="$(find_archive_tool)"
RANLIB_TOOL="$(find_ranlib_tool)"

cp -R "${TEMPLATE_DIR}" "${MAKE_PROJECT_DIR}"
make -C "${MAKE_PROJECT_DIR}" CC="${BUILD_DIR}/compiler" AR="${ARCHIVE_TOOL}"
assert_file_nonempty "${MAKE_PROJECT_DIR}/build/libnorthstar.a"

"${BUILD_DIR}/compiler" \
    -I"${MAKE_PROJECT_DIR}/include" \
    -DNORTHSTAR_SCALE=3 \
    "${MAKE_PROJECT_DIR}/app/main.c" \
    "${MAKE_PROJECT_DIR}/src/array.c" \
    "${MAKE_PROJECT_DIR}/src/stats.c" \
    "${MAKE_PROJECT_DIR}/src/sort.c" \
    "${MAKE_PROJECT_DIR}/src/search.c" \
    "${MAKE_PROJECT_DIR}/src/hash.c" \
    "${MAKE_PROJECT_DIR}/src/graph.c" \
    "${MAKE_PROJECT_DIR}/src/pipeline.c" \
    -o "${HOST_APP}"
assert_file_nonempty "${HOST_APP}"
run_and_check_output "${HOST_APP}"

if command -v cmake >/dev/null 2>&1 && command -v ninja >/dev/null 2>&1; then
    cp -R "${TEMPLATE_DIR}" "${CMAKE_SOURCE_DIR}"
    cat >"${TOOLCHAIN_FILE}" <<EOF
set(CMAKE_C_COMPILER "${BUILD_DIR}/compiler")
set(CMAKE_AR "${ARCHIVE_TOOL}")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_C_DEPFILE_FORMAT gcc)
set(CMAKE_C_DEPENDS_USE_COMPILER TRUE)
set(CMAKE_DEPFILE_FLAGS_C "-MMD -MT <DEP_TARGET> -MF <DEP_FILE> -MP")
EOF
    if [[ -n "${RANLIB_TOOL}" ]]; then
        printf 'set(CMAKE_RANLIB "%s")\n' "${RANLIB_TOOL}" >>"${TOOLCHAIN_FILE}"
    fi

    CFLAGS='-O0 -pipe -ffunction-sections -fdata-sections -fno-common -pthread' \
        cmake -S "${CMAKE_SOURCE_DIR}" -B "${CMAKE_BUILD_DIR}" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" >/dev/null
    cmake --build "${CMAKE_BUILD_DIR}" >/dev/null
    assert_file_nonempty "${CMAKE_BUILD_DIR}/libnorthstar.a"
else
    echo "skipped: cmake+ninja north-star build is unavailable"
fi

echo "verified: SysyCC can act as CC for a small 8-source-file Make/CMake static-library project and full-compile it as a runnable host executable"
