#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
CASE_BUILD_DIR="${SCRIPT_DIR}/build"
SOURCE_DIR="${CASE_BUILD_DIR}/src"
INCLUDE_DIR="${CASE_BUILD_DIR}/include"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

HOST_CC="$(command -v cc || command -v clang || true)"
HOST_AR="$(command -v ar || true)"
if [[ -z "${HOST_CC}" || -z "${HOST_AR}" ]]; then
    echo "skipped: cc/clang and ar are required for compiler multisource smoke"
    exit 0
fi

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
rm -rf "${CASE_BUILD_DIR}"
mkdir -p "${SOURCE_DIR}" "${INCLUDE_DIR}"

cat >"${INCLUDE_DIR}/project_config.h" <<'EOF'
#define PROJECT_BASE_VALUE 37
EOF

cat >"${SOURCE_DIR}/main.c" <<'EOF'
#include "project_config.h"

#ifndef PROJECT_OFFSET
#error PROJECT_OFFSET must be provided by the driver invocation
#endif

int helper_add_offset(int value);

int main(void) {
    return helper_add_offset(PROJECT_BASE_VALUE) == 39 ? 0 : 1;
}
EOF

cat >"${SOURCE_DIR}/helper.c" <<'EOF'
#include "project_config.h"

#ifndef PROJECT_OFFSET
#error PROJECT_OFFSET must be provided while compiling helper.c
#endif

int helper_add_offset(int value) {
    return value + PROJECT_OFFSET;
}
EOF

cat >"${SOURCE_DIR}/archive_helper.c" <<'EOF'
#include "project_config.h"

#ifndef PROJECT_OFFSET
#error PROJECT_OFFSET must be provided while compiling archive_helper.c
#endif

int helper_add_offset(int value) {
    return value + PROJECT_OFFSET;
}
EOF

"${BUILD_DIR}/compiler" \
    -I"${INCLUDE_DIR}" \
    -DPROJECT_OFFSET=2 \
    "${SOURCE_DIR}/main.c" \
    "${SOURCE_DIR}/helper.c" \
    -o "${CASE_BUILD_DIR}/app-direct"
"${CASE_BUILD_DIR}/app-direct"

"${HOST_CC}" \
    -I"${INCLUDE_DIR}" \
    -DPROJECT_OFFSET=2 \
    -c "${SOURCE_DIR}/helper.c" \
    -o "${CASE_BUILD_DIR}/helper.o"
"${BUILD_DIR}/compiler" \
    -I"${INCLUDE_DIR}" \
    -DPROJECT_OFFSET=2 \
    "${SOURCE_DIR}/main.c" \
    "${CASE_BUILD_DIR}/helper.o" \
    -o "${CASE_BUILD_DIR}/app-object"
"${CASE_BUILD_DIR}/app-object"

"${HOST_CC}" \
    -I"${INCLUDE_DIR}" \
    -DPROJECT_OFFSET=2 \
    -c "${SOURCE_DIR}/archive_helper.c" \
    -o "${CASE_BUILD_DIR}/archive_helper.o"
"${HOST_AR}" rcs "${CASE_BUILD_DIR}/libhelper.a" \
    "${CASE_BUILD_DIR}/archive_helper.o"
"${BUILD_DIR}/compiler" \
    -I"${INCLUDE_DIR}" \
    -DPROJECT_OFFSET=2 \
    "${SOURCE_DIR}/main.c" \
    "${CASE_BUILD_DIR}/libhelper.a" \
    -o "${CASE_BUILD_DIR}/app-archive"
"${CASE_BUILD_DIR}/app-archive"

assert_compiler_fails_with_message \
    "${BUILD_DIR}/compiler" \
    -c \
    "${SOURCE_DIR}/main.c" \
    "${SOURCE_DIR}/helper.c" \
    "multiple source inputs with -c are not supported yet; compile sources separately"

assert_compiler_fails_with_message \
    "${BUILD_DIR}/compiler" \
    -MD \
    "${SOURCE_DIR}/main.c" \
    "${SOURCE_DIR}/helper.c" \
    -o "${CASE_BUILD_DIR}/app-depfile" \
    "dependency generation with multiple source inputs is not supported yet; compile sources separately"

echo "verified: compiler full-compile driver links multiple sources, objects, archives, and rejects unsupported multisource modes"
