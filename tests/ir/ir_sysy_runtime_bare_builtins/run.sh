#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
TEST_SOURCE="${SCRIPT_DIR}/ir_sysy_runtime_bare_builtins.sy"
IR_OUTPUT_FILE="${BUILD_DIR}/intermediate_results/ir_sysy_runtime_bare_builtins.ll"
COMPILER_BIN="${BUILD_DIR}/compiler"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${COMPILER_BIN}" "${TEST_SOURCE}" --dump-ir >/dev/null

assert_file_nonempty "${IR_OUTPUT_FILE}"

grep -Fq "call void @_sysy_starttime(i32 2)" "${IR_OUTPUT_FILE}"
grep -Fq "call void (ptr, ...) @putf" "${IR_OUTPUT_FILE}"
grep -Fq "call void @_sysy_stoptime(i32 4)" "${IR_OUTPUT_FILE}"

echo "verified: bare SysY runtime builtins lower without forced sylib.h include"
