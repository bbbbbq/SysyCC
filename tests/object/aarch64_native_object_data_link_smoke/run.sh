#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
CASE_BUILD_DIR="${SCRIPT_DIR}/build"
INPUT_FILE="${SCRIPT_DIR}/aarch64_native_object_data_link_smoke.sy"
OBJ_FILE="${CASE_BUILD_DIR}/aarch64_native_object_data_link_smoke.o"
BIN_FILE="${CASE_BUILD_DIR}/aarch64_native_object_data_link_smoke.bin"
RUNTIME_SOURCE="${PROJECT_ROOT}/tests/run/support/runtime_target_only.c"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
mkdir -p "${CASE_BUILD_DIR}"

"${BUILD_DIR}/SysyCC" \
    --dump-tokens \
    --dump-parse \
    -c \
    --backend=aarch64-native \
    --target=aarch64-unknown-linux-gnu \
    -o "${OBJ_FILE}" \
    "${INPUT_FILE}"

assert_basic_frontend_outputs "${BUILD_DIR}" "aarch64_native_object_data_link_smoke"
assert_file_nonempty "${OBJ_FILE}"
READELF_TOOL="$(find_aarch64_readelf)"
"${READELF_TOOL}" -s "${OBJ_FILE}" | \
    grep -Eq 'OBJECT[[:space:]]+LOCAL[[:space:]]+DEFAULT[[:space:]]+[0-9]+ local_value$'
"${READELF_TOOL}" -s "${OBJ_FILE}" | \
    grep -Eq 'OBJECT[[:space:]]+LOCAL[[:space:]]+DEFAULT[[:space:]]+[0-9]+ local_ptr$'

link_status=0
link_aarch64_native_smoke_binary "${OBJ_FILE}" "${RUNTIME_SOURCE}" "${BIN_FILE}" || link_status=$?
if [[ "${link_status}" -eq 125 ]]; then
    echo "skipped: no AArch64 linker/sysroot for data-link smoke"
    exit 0
fi

assert_file_nonempty "${BIN_FILE}"

run_status=0
run_aarch64_native_smoke_if_available "${BIN_FILE}" "7" || run_status=$?
if [[ "${run_status}" -eq 125 ]]; then
    echo "skipped: no qemu/sysroot for data-link execute smoke"
    exit 0
fi

echo "verified: native AArch64 object emission now carries initialized local data through object linking"
