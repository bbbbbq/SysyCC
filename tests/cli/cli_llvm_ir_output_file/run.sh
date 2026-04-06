#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/cli_llvm_ir_output_file.sy"
OUTPUT_FILE="${BUILD_DIR}/cli_llvm_ir_output_file.ll"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
rm -f "${OUTPUT_FILE}"

"${BUILD_DIR}/compiler" -S -emit-llvm -o "${OUTPUT_FILE}" "${INPUT_FILE}"

grep -Fq "define i32 @main()" "${OUTPUT_FILE}"

echo "verified: -S -emit-llvm writes the public LLVM IR output to -o"
