#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_shadowed_local_name_uniquing.sy"
IR_FILE="${BUILD_DIR}/intermediate_results/ir_shadowed_local_name_uniquing.ll"
OBJECT_FILE="${BUILD_DIR}/ir_shadowed_local_name_uniquing.o"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ir

assert_file_nonempty "${IR_FILE}"
grep -Eq '^  %i\.addr = alloca i32$' "${IR_FILE}"
grep -Eq '^  %i\.addr1 = alloca i32$' "${IR_FILE}"
clang -c "${IR_FILE}" -o "${OBJECT_FILE}"

echo "verified: llvm lowering uniquifies shadowed local stack-slot names"
