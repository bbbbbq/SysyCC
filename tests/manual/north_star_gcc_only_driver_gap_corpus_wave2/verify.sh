#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
HOST_CC="${HOST_CC:-${CC:-cc}}"
SYSYCC="${BUILD_DIR}/compiler"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

for case_dir in "${SCRIPT_DIR}"/cases/*; do
    [[ -d "${case_dir}" ]] || continue
    case_name="$(basename "${case_dir}")"
    host_build="${case_dir}/build/host"
    sysycc_build="${case_dir}/build/sysycc"

    rm -rf "${case_dir}/build"
    mkdir -p "${case_dir}/build"

    make -C "${case_dir}" CC="${HOST_CC}" BUILD_DIR="${host_build}" >/dev/null
    "${host_build}/${case_name}" >/dev/null

    make -C "${case_dir}" CC="${SYSYCC}" BUILD_DIR="${sysycc_build}" \
        >/dev/null
    "${sysycc_build}/${case_name}" >/dev/null

    echo "verified driver compatibility wave2 pass: ${case_name}"
done

echo "verified: host compiler and SysyCC both accept all wave2 driver compatibility cases"
