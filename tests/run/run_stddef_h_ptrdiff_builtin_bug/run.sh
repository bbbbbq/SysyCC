#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/run_stddef_h_ptrdiff_builtin_bug.sy"
IR_FILE="${BUILD_DIR}/intermediate_results/run_stddef_h_ptrdiff_builtin_bug.ll"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
"${BUILD_DIR}/SysyCC" --dump-ir "${INPUT_FILE}"
assert_file_nonempty "${IR_FILE}"

echo "verified: stddef.h ptrdiff_t compiles successfully"
