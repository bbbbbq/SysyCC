#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/run_enum_increment_smoke.sy"
IR_FILE="${BUILD_DIR}/intermediate_results/run_enum_increment_smoke.ll"
BINARY_FILE="${BUILD_DIR}/intermediate_results/run_enum_increment_smoke"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" -S -emit-llvm -O2 -o "${IR_FILE}" --dump-ir >/dev/null
clang "${IR_FILE}" -o "${BINARY_FILE}"
"${BINARY_FILE}"

echo "verified: enum values are incrementable in prefix and postfix expressions"
