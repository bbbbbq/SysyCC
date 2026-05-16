#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
SRC="${SCRIPT_DIR}/mul_const_smoke.c"

if [[ -n "${SYSYCC_BUILD_DIR:-}" ]]; then
    BUILD_DIR="${SYSYCC_BUILD_DIR}"
elif [[ -x "${REPO_ROOT}/build/compiler" ]]; then
    BUILD_DIR="${REPO_ROOT}/build"
else
    BUILD_DIR="${REPO_ROOT}/build-ninja"
fi

COMPILER="${BUILD_DIR}/compiler"

if [[ ! -x "${COMPILER}" ]]; then
    echo "error: compiler not found at ${COMPILER}" >&2
    echo "hint: build SysyCC first, or set SYSYCC_BUILD_DIR=/path/to/build" >&2
    exit 1
fi

command -v aarch64-linux-gnu-gcc >/dev/null 2>&1 || {
    echo "error: aarch64-linux-gnu-gcc not found" >&2
    exit 1
}

command -v qemu-aarch64 >/dev/null 2>&1 || {
    echo "error: qemu-aarch64 not found" >&2
    exit 1
}

TMP_DIR="$(mktemp -d)"
trap 'rm -rf "${TMP_DIR}"' EXIT

run_one() {
    local opt="$1"
    local asm="${TMP_DIR}/mul_const_${opt#-}.s"
    local bin="${TMP_DIR}/mul_const_${opt#-}"

    echo "==> AArch64 multiply-by-constant smoke ${opt}"

    "${COMPILER}" "${opt}" -S "${SRC}" -o "${asm}"
    aarch64-linux-gnu-gcc "${asm}" -o "${bin}"
    qemu-aarch64 -L /usr/aarch64-linux-gnu "${bin}"
}


run_one -O0
run_one -O1

echo "AArch64 multiply-by-constant smoke passed"
