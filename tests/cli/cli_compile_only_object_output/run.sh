#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
CASE_BUILD_DIR="${SCRIPT_DIR}/build"
INPUT_FILE="${SCRIPT_DIR}/cli_compile_only_object_output.sy"
OBJ_FILE="${CASE_BUILD_DIR}/cli_compile_only_object_output.o"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
mkdir -p "${CASE_BUILD_DIR}"

"${BUILD_DIR}/SysyCC" \
    -c \
    --backend=aarch64-native \
    --target=aarch64-unknown-linux-gnu \
    -o "${OBJ_FILE}" \
    "${INPUT_FILE}"

assert_file_nonempty "${OBJ_FILE}"
READELF_TOOL="$(find_aarch64_readelf)"
"${READELF_TOOL}" -S "${OBJ_FILE}" | grep -q '\.text'
"${READELF_TOOL}" -s "${OBJ_FILE}" | grep -q ' add$'

echo "verified: -c now emits a native AArch64 ELF object file"
