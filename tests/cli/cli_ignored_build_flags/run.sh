#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
CASE_BUILD_DIR="${SCRIPT_DIR}/build"
INPUT_FILE="${SCRIPT_DIR}/cli_ignored_build_flags.c"
OBJ_FILE="${CASE_BUILD_DIR}/cli_ignored_build_flags.o"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
mkdir -p "${CASE_BUILD_DIR}"

"${BUILD_DIR}/compiler" \
    -pipe \
    -Winvalid-pch \
    -ffunction-sections \
    -fdata-sections \
    -fno-common \
    -fno-builtin-memcmp \
    -falign-functions=16 \
    -fvisibility=hidden \
    -flto=auto \
    -grecord-gcc-switches \
    -Wfatal-errors \
    -Wdisabled-optimization \
    -Wc++-compat \
    -Wdate-time \
    -Wformat-truncation=2 \
    -Werror=implicit-function-declaration \
    -march=native \
    -arch arm64 \
    -c \
    -o "${OBJ_FILE}" \
    "${INPUT_FILE}"

assert_file_nonempty "${OBJ_FILE}"

echo "verified: common GCC-like build flags in the safe-ignore bucket do not break compile-only output"
