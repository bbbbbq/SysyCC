#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${SYSYCC_BUILD_DIR:-${PROJECT_ROOT}/build}"
INPUT_FILE="${SCRIPT_DIR}/run_local_static_storage_duration.sy"
IR_FILE="${BUILD_DIR}/intermediate_results/run_local_static_storage_duration.ll"
BINARY_FILE="${BUILD_DIR}/intermediate_results/run_local_static_storage_duration"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
"${BUILD_DIR}/SysyCC" -O -S -emit-llvm "${INPUT_FILE}" -o "${IR_FILE}"
grep -q "@get_entries.entries.static" "${IR_FILE}"
if grep -q "alloca \\[2 x" "${IR_FILE}"; then
    echo "error: local static array was emitted as a stack alloca" >&2
    exit 1
fi
grep -q "inttoptr (i64 1 to ptr)" "${IR_FILE}"
clang "${IR_FILE}" -Wno-override-module -o "${BINARY_FILE}"
"${BINARY_FILE}"

echo "verified: function-scope static variables use static storage duration"
