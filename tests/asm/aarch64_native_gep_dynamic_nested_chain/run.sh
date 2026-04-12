#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
TEST_BUILD_DIR="${SCRIPT_DIR}/build"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
INPUT_FILE="${SCRIPT_DIR}/${TEST_NAME}.sy"
TEST_SOURCE="${SCRIPT_DIR}/${TEST_NAME}.cpp"
TEST_BINARY="${TEST_BUILD_DIR}/${TEST_NAME}"
ASM_FILE="${TEST_BUILD_DIR}/${TEST_NAME}.s"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
mkdir -p "${TEST_BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"

OBJECT_FILES=()
while IFS= read -r -d '' object_file; do
    OBJECT_FILES+=("${object_file}")
done < <(find_host_compiler_object_files "${BUILD_DIR}")

clang++ -std=c++17 -I"${PROJECT_ROOT}" -I"${PROJECT_ROOT}/src" \
    "${TEST_SOURCE}" \
    "${OBJECT_FILES[@]}" \
    -L"${BUILD_DIR}" \
    -lsysycc_aarch64_codegen \
    -Wl,-rpath,"${BUILD_DIR}" \
    -o "${TEST_BINARY}"

"${TEST_BINARY}" >"${ASM_FILE}"
assert_file_nonempty "${ASM_FILE}"

assert_no_illegal_aarch64_index_forms "${ASM_FILE}"
grep -Eq '^  mul x[0-9]+, x[0-9]+, x[0-9]+$' "${ASM_FILE}"

echo "verified: native AArch64 asm lowers nested dynamic GEP chains through pointer-width scaling, including non-power-of-two strides"
