#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_nested_member_gep_preserves_source_type.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
"${BUILD_DIR}/compiler" -O1 -S -emit-llvm "${INPUT_FILE}" -o "${BUILD_DIR}/${TEST_NAME}.ll"

if grep -Fq "getelementptr inbounds void" "${BUILD_DIR}/intermediate_results/${TEST_NAME}.ll"; then
    echo "error: nested member GEP lost its source pointee type" >&2
    exit 1
fi

echo "verified: flattened nested member GEPs preserve source pointee types"
