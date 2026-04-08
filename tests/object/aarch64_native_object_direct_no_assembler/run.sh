#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
CASE_BUILD_DIR="${SCRIPT_DIR}/build"
INPUT_FILE="${SCRIPT_DIR}/aarch64_native_object_direct_no_assembler.sy"
OBJ_FILE="${CASE_BUILD_DIR}/aarch64_native_object_direct_no_assembler.o"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
mkdir -p "${CASE_BUILD_DIR}"

PATH="/usr/bin:/bin" \
    "${BUILD_DIR}/SysyCC" \
    -c \
    --backend=aarch64-native \
    --target=aarch64-unknown-linux-gnu \
    -o "${OBJ_FILE}" \
    "${INPUT_FILE}"

assert_file_nonempty "${OBJ_FILE}"
READELF_TOOL="$(find_aarch64_readelf)"
"${READELF_TOOL}" -S "${OBJ_FILE}" | grep -q '\.text'
"${READELF_TOOL}" -S "${OBJ_FILE}" | grep -q '\.data'

echo "verified: native AArch64 direct object emission no longer depends on an external assembler in PATH"
