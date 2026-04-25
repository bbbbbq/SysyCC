#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
CASE_BUILD_DIR="${SCRIPT_DIR}/build"
SOURCE_DIR="${CASE_BUILD_DIR}/src"
INCLUDE_DIR="${CASE_BUILD_DIR}/include"
MAKE_BUILD_DIR="${CASE_BUILD_DIR}/make-build"
NINJA_BUILD_DIR="${CASE_BUILD_DIR}/ninja-build"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

HOST_CC="$(command -v cc || command -v clang || true)"
if [[ -z "${HOST_CC}" ]]; then
    echo "skipped: cc or clang is required for compiler make/ninja smoke"
    exit 0
fi

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
rm -rf "${CASE_BUILD_DIR}"
mkdir -p "${SOURCE_DIR}" "${INCLUDE_DIR}" "${MAKE_BUILD_DIR}" "${NINJA_BUILD_DIR}"

cat >"${INCLUDE_DIR}/project_config.h" <<'EOF'
#define PROJECT_BASE_VALUE 40
EOF

cat >"${SOURCE_DIR}/helper.c" <<'EOF'
#include "project_config.h"

#ifndef PROJECT_OFFSET
#error PROJECT_OFFSET must be provided by the build system
#endif

int helper_add(int lhs, int rhs) {
    return lhs + rhs + PROJECT_OFFSET - PROJECT_OFFSET;
}
EOF

cat >"${SOURCE_DIR}/main.c" <<'EOF'
#include "project_config.h"

#ifndef PROJECT_OFFSET
#error PROJECT_OFFSET must be provided by the build system
#endif

int helper_add(int lhs, int rhs);

int main(void) {
    return helper_add(PROJECT_BASE_VALUE, PROJECT_OFFSET) == 42 ? 0 : 1;
}
EOF

cat >"${MAKE_BUILD_DIR}/Makefile" <<EOF
SYSYCC := ${BUILD_DIR}/compiler
HOST_CC := ${HOST_CC}
SRC_DIR := ${SOURCE_DIR}
INCLUDE_DIR := ${INCLUDE_DIR}

all: app app-multisource

helper.o: \$(SRC_DIR)/helper.c
	\$(HOST_CC) -I\$(INCLUDE_DIR) -DPROJECT_OFFSET=2 -c \$< -o \$@

app: \$(SRC_DIR)/main.c helper.o
	\$(SYSYCC) -I\$(INCLUDE_DIR) -DPROJECT_OFFSET=2 \$< helper.o -o \$@

app-multisource: \$(SRC_DIR)/main.c \$(SRC_DIR)/helper.c
	\$(SYSYCC) -I\$(INCLUDE_DIR) -DPROJECT_OFFSET=2 \$^ -o \$@

clean:
	rm -f app app-multisource helper.o
EOF

make -C "${MAKE_BUILD_DIR}" all
"${MAKE_BUILD_DIR}/app"
"${MAKE_BUILD_DIR}/app-multisource"

if command -v ninja >/dev/null 2>&1; then
    cat >"${NINJA_BUILD_DIR}/build.ninja" <<EOF
rule host_cc
  command = ${HOST_CC} -I${INCLUDE_DIR} -DPROJECT_OFFSET=2 -c \$in -o \$out

rule sysycc_link
  command = ${BUILD_DIR}/compiler -I${INCLUDE_DIR} -DPROJECT_OFFSET=2 \$in -o \$out

rule sysycc_link_multisource
  command = ${BUILD_DIR}/compiler -I${INCLUDE_DIR} -DPROJECT_OFFSET=2 \$in -o \$out

build helper.o: host_cc ${SOURCE_DIR}/helper.c
build app: sysycc_link ${SOURCE_DIR}/main.c helper.o
build app-multisource: sysycc_link_multisource ${SOURCE_DIR}/main.c ${SOURCE_DIR}/helper.c

default app app-multisource
EOF
    ninja -C "${NINJA_BUILD_DIR}"
    "${NINJA_BUILD_DIR}/app"
    "${NINJA_BUILD_DIR}/app-multisource"
fi

echo "verified: make/ninja can invoke SysyCC with -I/-D/-o, external objects, and direct multisource linking"
