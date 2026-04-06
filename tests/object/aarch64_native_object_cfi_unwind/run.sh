#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
CASE_BUILD_DIR="${SCRIPT_DIR}/build"
INPUT_FILE="${SCRIPT_DIR}/aarch64_native_object_cfi_unwind.sy"
OBJ_FILE="${CASE_BUILD_DIR}/aarch64_native_object_cfi_unwind.o"

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

assert_basic_frontend_outputs "${BUILD_DIR}" "aarch64_native_object_cfi_unwind"
assert_file_nonempty "${OBJ_FILE}"
READELF_TOOL="$(find_aarch64_readelf)"
"${READELF_TOOL}" -S "${OBJ_FILE}" | grep -q '\.eh_frame'
"${READELF_TOOL}" -wf "${OBJ_FILE}" | grep -Eq 'r19 \(x19\)'
"${READELF_TOOL}" -wf "${OBJ_FILE}" | grep -Eq 'r72 \(v8\)'

echo "verified: native AArch64 object emission now carries unwind frame information for allocator-inserted callee-saved registers"
