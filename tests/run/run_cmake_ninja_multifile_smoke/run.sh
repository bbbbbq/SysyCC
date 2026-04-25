#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
CASE_BUILD_DIR="${SCRIPT_DIR}/build"
SOURCE_DIR="${CASE_BUILD_DIR}/src"
BUILD_TREE="${CASE_BUILD_DIR}/cmake-build"
TEMPLATE_DIR="${SCRIPT_DIR}/project"
TOOLCHAIN_FILE="${CASE_BUILD_DIR}/sysycc-toolchain.cmake"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

if ! command -v cmake >/dev/null 2>&1; then
    echo "skipped: cmake is required for the CMake+Ninja SysyCC driver smoke"
    exit 0
fi

if ! command -v ninja >/dev/null 2>&1; then
    echo "skipped: ninja is required for the CMake+Ninja SysyCC driver smoke"
    exit 0
fi

mtime_of() {
    local path="$1"
    if stat -f '%m' "${path}" >/dev/null 2>&1; then
        stat -f '%m' "${path}"
        return 0
    fi
    stat -c '%Y' "${path}"
}

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
rm -rf "${SOURCE_DIR}" "${BUILD_TREE}"
mkdir -p "${CASE_BUILD_DIR}"
cp -R "${TEMPLATE_DIR}" "${SOURCE_DIR}"

cat > "${TOOLCHAIN_FILE}" <<EOF
set(CMAKE_C_COMPILER "${BUILD_DIR}/compiler")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_C_DEPFILE_FORMAT gcc)
set(CMAKE_C_DEPENDS_USE_COMPILER TRUE)
set(CMAKE_DEPFILE_FLAGS_C "-MMD -MT <DEP_TARGET> -MF <DEP_FILE> -MP")
EOF

CFLAGS='-pipe -Winvalid-pch -ffunction-sections -fdata-sections -fno-common -fvisibility=hidden -pthread' \
cmake -S "${SOURCE_DIR}" -B "${BUILD_TREE}" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" >/dev/null

BUILD_LOG1="${CASE_BUILD_DIR}/cmake-build-1.log"
BUILD_LOG2="${CASE_BUILD_DIR}/cmake-build-2.log"
BUILD_LOG3="${CASE_BUILD_DIR}/cmake-build-3.log"

cmake --build "${BUILD_TREE}" >"${BUILD_LOG1}" 2>&1

ADD_OBJECT="${BUILD_TREE}/CMakeFiles/demo.dir/add.c.o"
MUL_OBJECT="${BUILD_TREE}/CMakeFiles/demo.dir/mul.c.o"
ARCHIVE_FILE="${BUILD_TREE}/libdemo.a"

assert_file_nonempty "${ADD_OBJECT}"
assert_file_nonempty "${MUL_OBJECT}"
assert_file_nonempty "${ARCHIVE_FILE}"

ADD_MTIME_1="$(mtime_of "${ADD_OBJECT}")"
MUL_MTIME_1="$(mtime_of "${MUL_OBJECT}")"
ARCHIVE_MTIME_1="$(mtime_of "${ARCHIVE_FILE}")"

cmake --build "${BUILD_TREE}" >"${BUILD_LOG2}" 2>&1

ADD_MTIME_2="$(mtime_of "${ADD_OBJECT}")"
MUL_MTIME_2="$(mtime_of "${MUL_OBJECT}")"
ARCHIVE_MTIME_2="$(mtime_of "${ARCHIVE_FILE}")"

[[ "${ADD_MTIME_1}" == "${ADD_MTIME_2}" ]]
[[ "${MUL_MTIME_1}" == "${MUL_MTIME_2}" ]]
[[ "${ARCHIVE_MTIME_1}" == "${ARCHIVE_MTIME_2}" ]]
grep -Fq "ninja: no work to do." "${BUILD_LOG2}"

sleep 1
cat > "${SOURCE_DIR}/add_private.h" <<'EOF'
#ifndef RUN_CMAKE_NINJA_MULTIFILE_SMOKE_ADD_PRIVATE_H
#define RUN_CMAKE_NINJA_MULTIFILE_SMOKE_ADD_PRIVATE_H

#define ADD_PRIVATE_OFFSET 1

#endif
EOF

cmake --build "${BUILD_TREE}" >"${BUILD_LOG3}" 2>&1

ADD_MTIME_3="$(mtime_of "${ADD_OBJECT}")"
MUL_MTIME_3="$(mtime_of "${MUL_OBJECT}")"
ARCHIVE_MTIME_3="$(mtime_of "${ARCHIVE_FILE}")"

[[ "${ADD_MTIME_3}" -gt "${ADD_MTIME_2}" ]]
[[ "${MUL_MTIME_3}" == "${MUL_MTIME_2}" ]]
[[ "${ARCHIVE_MTIME_3}" -gt "${ARCHIVE_MTIME_2}" ]]
grep -Fq "add.c.o" "${BUILD_LOG3}"
if grep -Fq "mul.c.o" "${BUILD_LOG3}"; then
    echo "error: cmake+ninja incremental rebuild unexpectedly rebuilt mul.c.o" >&2
    cat "${BUILD_LOG3}" >&2
    exit 1
fi

echo "verified: a small CMake+Ninja static-library project can compile-only build with SysyCC, stay no-op on the second build, and selectively rebuild after a header change"
