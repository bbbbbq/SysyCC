#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
TEST_NAME="run_local_struct_multi_decl_initializer"
INPUT_FILE="${SCRIPT_DIR}/${TEST_NAME}.sy"
IR_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.ll"
BINARY_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" -S -emit-llvm "${INPUT_FILE}" -o "${IR_FILE}"
clang "${IR_FILE}" -o "${BINARY_FILE}"

set +e
"${BINARY_FILE}"
rc=$?
set -e

if [[ "${rc}" -ne 225 ]]; then
    echo "expected exit code 4321 modulo 256 (225), got ${rc}" >&2
    exit 1
fi

echo "verified: local struct initializer consumes multi-declarator scalar fields"
