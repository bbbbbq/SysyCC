#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
TEST_BUILD_DIR="${SCRIPT_DIR}/build"
TEST_BINARY="${TEST_BUILD_DIR}/ir_core_gvn_dominated_compare"
TEST_SOURCE="${SCRIPT_DIR}/ir_core_gvn_dominated_compare.cpp"
INPUT_FILE="${SCRIPT_DIR}/ir_core_gvn_dominated_compare.sy"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

mkdir -p "${TEST_BUILD_DIR}"
"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ir

OBJECT_FILES=()
while IFS= read -r -d '' object_file; do
    OBJECT_FILES+=("${object_file}")
done < <(find "${BUILD_DIR}/CMakeFiles/SysyCC.dir" \
    -name '*.o' ! -name 'main.cpp.o' -print0)

clang++ -std=c++17 -I"${PROJECT_ROOT}/src" \
    "${TEST_SOURCE}" \
    "${OBJECT_FILES[@]}" \
    -o "${TEST_BINARY}"

"${TEST_BINARY}"

echo "verified: GVN reuses dominated compare computations"
