#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/cli_multiarch_sysroot_include.c"
SYSROOT_DIR="${BUILD_DIR}/test-work/cli_multiarch_sysroot_include/sysroot"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

rm -rf "${SYSROOT_DIR}"
mkdir -p "${SYSROOT_DIR}/usr/include/aarch64-linux-gnu/bits"

cat >"${SYSROOT_DIR}/usr/include/limits.h" <<'HEADER'
#include <bits/libc-header-start.h>
#define SYSYCC_MULTIARCH_LIMIT_VALUE SYSYCC_MULTIARCH_BITS_VALUE
HEADER

cat >"${SYSROOT_DIR}/usr/include/aarch64-linux-gnu/bits/libc-header-start.h" <<'HEADER'
#define SYSYCC_MULTIARCH_BITS_VALUE 314
HEADER

set +e
OUTPUT="$("${BUILD_DIR}/compiler" --sysroot "${SYSROOT_DIR}" -E "${INPUT_FILE}" 2>&1)"
RC=$?
set -e

if [[ ${RC} -ne 0 ]]; then
    echo "error: multiarch sysroot include unexpectedly failed" >&2
    echo "${OUTPUT}" >&2
    exit 1
fi

grep -Fq "int value = 314;" <<<"${OUTPUT}"

echo "verified: --sysroot adds Linux multiarch include directories before generic usr/include"
